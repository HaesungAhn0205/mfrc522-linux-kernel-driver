#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/slab.h>

#define DEVICE_NAME "rc522_rfid"
#define CLASS_NAME  "rfid_class"
#define MAX_LEN 16

// MFRC522 Registers
#define CommandReg    0x01
#define ComIEnReg     0x02
#define ComIrqReg     0x04
#define ErrorReg      0x06
#define FIFODataReg   0x09
#define FIFOLevelReg  0x0A
#define ControlReg    0x0C
#define BitFramingReg 0x0D
#define ModeReg       0x11
#define TxModeReg     0x12
#define RxModeReg     0x13
#define TxControlReg  0x14
#define TxASKReg      0x15
#define VersionReg    0x37

static struct spi_device *rc522_spi_device = NULL;
static int majorNumber;
static struct class* rfidClass = NULL;
static struct device* rfidDevice = NULL;

/* --- 기본 레지스터 제어 --- */

// 데이터 쓰겠다 신호, shift left 1bit, lsb = 0으로 해서 write
static void WriteReg(uint8_t addr, uint8_t val) { 
    uint8_t tx[2] = { (addr << 1) & 0x7E, val };
    spi_write(rc522_spi_device, tx, 2);
}
// 데이터 읽겠다 신호, shift left 1bit, lsb = 1으로 해서 read
static uint8_t ReadReg(uint8_t addr) {
    uint8_t tx = ((addr << 1) & 0x7E) | 0x80, rx = 0;
    spi_write_then_read(rc522_spi_device, &tx, 1, &rx, 1);
    return rx;
}

//set,clear 함수 : WriteReg(reg, 0x03) 이렇게 쓰지 않는다!
static void SetBitMask(uint8_t reg, uint8_t mask) {
    WriteReg(reg, ReadReg(reg) | mask);
}

static void ClearBitMask(uint8_t reg, uint8_t mask) {
    WriteReg(reg, ReadReg(reg) & (~mask));
}

/* --- 로직 이식 (ToCard) --- */

static int ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen, uint8_t *backData, uint8_t *backLen) {
    uint8_t irqEn = 0x00, waitIRq = 0x00, n;
    int i;

    if (command == 0x0E) { irqEn = 0x12; waitIRq = 0x10; } // PCD_AUTHENT
    else if (command == 0x0C) { irqEn = 0x77; waitIRq = 0x30; } // PCD_TRANSCEIVE

    WriteReg(ComIEnReg, irqEn | 0x80); // 인터럽트 설정, 통신 완료나 에러 시 알려달라고 설정
    ClearBitMask(ComIrqReg, 0x80);
    SetBitMask(FIFOLevelReg, 0x80); //이전 데이터 비움
    WriteReg(CommandReg, 0x00); // PCD_IDLE

    for (i = 0; i < sendLen; i++) WriteReg(FIFODataReg, sendData[i]); // 카드에 보낼 명령을 통로에 1byte씩 넣음
		
		//command가 0C이면 tranceive, BitFramingReg의 0x80을 켜서 "전송 시작" 선언
    WriteReg(CommandReg, command);
    if (command == 0x0C) SetBitMask(BitFramingReg, 0x80); 

    i = 2000;
    while (1) {
        n = ReadReg(ComIrqReg);
        if (n & waitIRq) break; //카드가 대답하거나
        if (n & 0x01 || --i == 0) return 0; // Timeout이거나 루프 횟수 초과되면 빠져나옴 
        udelay(100);
    }

    ClearBitMask(BitFramingReg, 0x80);
    if (ReadReg(ErrorReg) & 0x1B) return 0; //에러가 없으면

    n = ReadReg(FIFOLevelReg); // 카드가 보낸 데이터가 몇 바이트인지 확인
    if (n > MAX_LEN) n = MAX_LEN; // 그 데이터를 backData에 담기
    *backLen = n;
    for (i = 0; i < n; i++) backData[i] = ReadReg(FIFODataReg);

    return 1;
}

/* --- 캐릭터 디바이스 구현 (read 호출 시 카드 읽기) --- */
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    uint8_t uid[MAX_LEN], backLen, status;
    uint8_t tagType = 0x26; // PICC_REQIDL

    // 1. Request
    WriteReg(BitFramingReg, 0x07);
    status = ToCard(0x0C, &tagType, 1, uid, &backLen); //카드 호출, 성공하면 다음으로
    if (!status) return 0;

    // 2. Anticoll
    WriteReg(BitFramingReg, 0x00);
    uid[0] = 0x93; uid[1] = 0x20; // PICC_ANTICOLL, 카드번호 내놔
    status = ToCard(0x0C, uid, 2, uid, &backLen);

    if (status && backLen >= 4) { // 받은 데이터가 4바이트 이상이면
        if (copy_to_user(buffer, uid, 4)) return -EFAULT;
        return 4; // 4바이트 UID 반환
    }
    return 0;
}

static struct file_operations fops = { .owner = THIS_MODULE, .read = dev_read };

/* --- 드라이버 생명주기 --- */
// 장치를 등록하고 rc422_rfid 파일 생성
static int rc522_probe(struct spi_device *spi) {
    rc522_spi_device = spi;
    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
    rfidClass = class_create(CLASS_NAME);
    rfidDevice = device_create(rfidClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
//*주번호(major num): 특정 디바이스 드라이버를 식별하는 고유한 번호

    // 하드웨어 초기화
    WriteReg(CommandReg, 0x0F); // PCD_RESETPHASE
    msleep(10);
    WriteReg(0x2A, 0x80); WriteReg(0x2B, 0xA9); // Timer
    WriteReg(0x2C, 0x03); WriteReg(0x2D, 0xE8);
    WriteReg(TxASKReg, 0x40); WriteReg(ModeReg, 0x3D);
    SetBitMask(TxControlReg, 0x03); // Antenna On

    printk(KERN_INFO "RC522: Driver Loaded. /dev/%s ready.\n", DEVICE_NAME);
    return 0;
}

//장치 제거 or rmmod 했을 때 실행. 클래스부터 주번호를 삭제해서 메모리 누수 방지
static void rc522_remove(struct spi_device *spi) {
    device_destroy(rfidClass, MKDEV(majorNumber, 0));
    class_unregister(rfidClass);
    class_destroy(rfidClass);
    unregister_chrdev(majorNumber, DEVICE_NAME);
}

// 장치 트리와 이 코드를 연결하는 트리
//장치 트리 오버레이 파일에 적은 이름이랑 여기 nxp,rc522가 같으면 probe 시랳ㅇ
static const struct of_device_id rc522_of_match[] = { { .compatible = "nxp,rc522", }, { } };
MODULE_DEVICE_TABLE(of, rc522_of_match);

static struct spi_driver rc522_driver = {
    .driver = { .name = "rc522", .of_match_table = rc522_of_match, .owner = THIS_MODULE },
    .probe = rc522_probe, .remove = rc522_remove, 
};

module_spi_driver(rc522_driver);
MODULE_LICENSE("GPL");