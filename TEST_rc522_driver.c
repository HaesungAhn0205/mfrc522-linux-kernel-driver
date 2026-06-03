#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#define DEVICE_PATH "/dev/rc522_rfid"

int main() {
    int fd;
    uint8_t uid[4];
    ssize_t ret;

    // 1. 드라이버 파일 열기
    fd = open(DEVICE_PATH, O_RDONLY);
    if (fd < 0) {
        perror("장치 파일을 열 수 없습니다. (sudo 권한 확인)");
        return -1;
    }

    printf("--- RFID 드라이버 테스트 앱 시작 ---\n");
    printf("카드를 리더기에 대 주세요...\n");

    while (1) {
        // 2. 드라이버로부터 4바이트(UID) 읽기 시도
        ret = read(fd, uid, 4);

        if (ret == 4) {
            // 읽기 성공: UID 출력
            printf("\n[카드 감지!] UID: %02X %02X %02X %02X\n",
                uid[0], uid[1], uid[2], uid[3]);

            // 너무 빨리 찍히는 것을 방지하기 위한 딜레이
            usleep(500000);
        }
        else if (ret < 0) {
            perror("읽기 오류");
            break;
        }

        // CPU 점유율을 낮추기 위한 짧은 대기 (0.1초)
        usleep(100000);
    }

    close(fd);
    return 0;
}