# mfrc522-linux-kernel-driver
MFRC522 RFID SPI 클라이언트 드라이버 및 라즈베리파이 5 장치 트리 오버레이 구현 프로젝트

# MFRC522 RFID Linux Kernel Driver & SPI Device Control

라즈베리파이 5(Raspberry Pi 5) 환경에서 MFRC522 RFID 모듈을 제어하기 위한 **리눅스 커널 스페이스 드라이버(SPI 기반 캐릭터 디바이스)** 및 유저 스페이스 테스트 애플리케이션 개발 프로젝트입니다. 

단순히 라이브러리를 가져다 쓰는 것을 넘어, MFRC522 내부 레지스터 구조와 SPI 통신 프로토콜 규격을 데이터시트 기반으로 직접 분석하고 구현하며 하드웨어-커널-유저 공간 인터페이스의 메커니즘을 이해하는 것을 목표로 진행했습니다.

---

## 🚀 주요 기능 및 특징
- **리눅스 커널 드라이버 구현**: 커널 스페이스에서 구동되는 SPI 클라이언트 드라이버 설계
- **장치 트리 오버레이 (Device Tree Overlay)**: 라즈베리파이 5 칩셋(`BCM2712`)의 기본 `spidev`를 비활성화하고 자체 드라이버를 `nxp,rc522` 호환성 매핑으로 동적 바인딩
- **캐릭터 디바이스 인터페이스**: 유저 공간에서 표준 파일 연산(`open`, `read`)을 통해 RFID UID(4바이트) 데이터에 동기식 접근 가능
- **하드웨어 튜닝**: 전력 전송 극대화를 위한 안테나 100% ASK 변조 방식(TxASKReg) 및 타이머 프레임 오버플로우 제어 적용

---

## 🛠 하드웨어 연결 (Pin Mapping)
| MFRC522 핀 | 라즈베리파이 5 (BCM) | 비고 |
| :--- | :--- | :--- |
| **SDA (SS)** | GPIO 8 (Pin 24) | SPI0 CE0 |
| **SCK** | GPIO 11 (Pin 23) | SPI0 SCLK |
| **MOSI** | GPIO 10 (Pin 19) | SPI0 MOSI |
| **MISO** | GPIO 9 (Pin 21) | SPI0 MISO |
| **GND** | GND (Pin 6) | 접지 |
| **RST** | 3.3V (Pin 17) | 하드웨어 리셋 (Always Awake) |
| **3.3V** | 3.3V (Pin 1) | 5V 인가 금지 |

---

## 🔍 핵심 트러블슈팅 및 깨달은 점

### 1. SPI Read Address 비트 연산 프로토콜 매핑 오류
- **문제 현상**: 최초 유저 스페이스 테스트 당시 레지스터 읽기 명령(`0xB7`) 전송 시 칩 버전 정보(`VersionReg, 0x37`)를 읽어오지 못하고 무응답(0x00) 출력 현상 발생.
- **원인 분석**: MFRC522 데이터시트 확인 결과, SPI 통신 시 주소 바이트의 **MSB(Bit 7)가 1이면 Read, 0이면 Write**로 규정되어 있으며, 주소 값이 1비트 좌측 시프트되어야 함을 식별.
- **해결 방법**: `(0x37 << 1) | 0x80` 연산을 통해 올바른 읽기 커맨드가 **`0xEE`**임을 도출하여 반영 후 하드웨어 버전(0x91/0x0x92) 정상 정상 인식 확인. 데이터시트 스펙을 면밀히 분석하는 습관의 중요성을 체감함.

### 2. 안테나 유도 전류 공급 부족으로 인한 카드 인식 실패
- **문제 현상**: 초기 C 구현 코드에서 Request 명령을 날려도 패시브 태그가 응답하지 않는 문제 발생.
- **원인 분석**: 안테나 구동 제어 레지스터(`TxControlReg`, `TxASKReg`) 설정 타이밍 문제와 타이머 분주기(Prescaler) 값 불일치로 인해 카드 내부 코일을 깨울 수 있는 충분한 유도 전류(13.56MHz HF 자기장)가 생성되지 못함.
- **해결 방법**: 교수님 코드 및 데이터시트 분석을 통해 Soft Reset 이후 `ModWidthReg(0x24)`, 타이머 프리scaler(`TModeReg`, `TPrescalerReg`) 설정을 먼저 완료하고, 송수신 모드를 초기화한 상태에서 안테나를 활성화(`AntennaOn`)하도록 하드웨어 초기화 시퀀스를 재설계하여 안정적인 무선 신호 변조 파형 확보.

---

## 📂 파일 구조
- `rc522_driver_SPI.c`: 캐릭터 디바이스 인터페이스를 포함한 리눅스 커널 SPI 드라이버 소스
- `rc522.dts`: SPI0 커널 드라이버 매핑을 위한 장치 트리 소스 파일
- `TEST_rc522_driver.c`: `/dev/rc522_rfid`를 폴링하며 실시간 UID를 출력하는 유저 테스트 앱
- `Makefile`: 커널 모듈 빌드 자동화 스크립트

---

## 💻 실행 및 배포 방법

### 1. 장치 트리 오버레이 컴파일 및 반영
```bash
# dts를 dtbo로 컴파일
dtc -@ -I dts -O dtb -o rc522.dtbo rc522.dts

# 시스템 오버레이 폴더로 복사
sudo cp rc522.dtbo /boot/firmware/overlays/

# /boot/firmware/config.txt 파일 하단에 설정 추가 후 재부팅
# dtoverlay=rc522
```

### 2. 커널 모듈 빌드 및 적재
```bash
# 모듈 컴파일
make

# 드라이버 적재 및 노드 권한 설정
sudo insmod rc522_driver.ko
sudo chmod 666 /dev/rc522_rfid
```

### 3. 유저 테스트 어플리케이션 실행
```bash
gcc TEST_rc522_driver.c -o rfid_test
./rfid_test
```
