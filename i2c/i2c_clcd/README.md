# I2C CLCD 드라이버 및 애플리케이션 분석

본 문서는 I2C 인터페이스를 사용하는 캐릭터 LCD (CLCD)를 위한 리눅스 디바이스 드라이버와 사용자 공간 애플리케이션에 대한 분석 내용을 담고 있습니다. 프로젝트는 크게 **디바이스 드라이버**와 **사용자 애플리케이션** 두 부분으로 구성됩니다.

## 1. I2C 디바이스 드라이버

디바이스 드라이버는 커널 공간에서 동작하며, 하드웨어를 직접 제어하고 사용자 공간에 인터페이스를 제공합니다. 이 프로젝트의 드라이버는 다시 **디바이스 트리 오버레이**와 **커널 모듈**로 나뉩니다.

### 1.1. 디바이스 트리 오버레이 (`device_tree/clcd-overlay.dts`)

디바이스 트리(Device Tree)는 시스템의 하드웨어 구성을 기술하는 데이터 구조입니다. `clcd-overlay.dts` 파일은 I2C 버스에 연결된 CLCD 장치를 커널에 등록하기 위한 오버레이입니다.

**주요 내용 분석:**

- **`compatible = "my-i2c,pcf8574-hd44780";`**: 이 드라이버가 어떤 장치를 지원하는지를 나타내는 호환 문자열입니다. 커널은 이 문자열을 보고 `i2c_clcd_dev.c` 드라이버와 장치를 연결합니다.
- **`reg = <0x27>;`**: CLCD 모듈의 I2C 주소를 0x27로 지정합니다.
- **`status = "okay";`**: 해당 장치가 활성화 상태임을 나타냅니다.
- **`fragment@0` 및 `target = <&i2c1>;`**: 이 오버레이가 `i2c1` 버스에 적용됨을 명시합니다. 즉, i2c-1 버스에 0x27 주소로 연결된 장치를 등록하라는 의미입니다.

`device_tree/Makefile`은 `dtc` (Device Tree Compiler)를 사용하여 이 `.dts` 소스 파일을 컴파일하고, 커널이 사용할 수 있는 `.dtbo` (Device Tree Blob Overlay) 바이너리 파일을 생성하는 역할을 합니다.

### 1.2. 커널 모듈 드라이버 (`driver_app/i2c_clcd_dev.c`)

이 파일은 CLCD의 핵심 제어 로직을 담고 있는 커널 모듈입니다. 사용자 애플리케이션으로부터 데이터를 받아 I2C 통신을 통해 CLCD에 텍스트를 출력하는 역할을 합니다.

**주요 코드 분석:**

- **`i2c_driver` 구조체 (`i2c_clcd_driver`)**:
    - `.driver`: 드라이버의 이름("i2c_clcd")과 소유자(THIS_MODULE)를 지정합니다.
    - `.probe`: I2C 버스에서 `compatible` 문자열과 일치하는 장치가 발견되었을 때 호출될 함수(`i2c_clcd_probe`)를 지정합니다.
    - `.remove`: 장치가 제거될 때 호출될 함수(`i2c_clcd_remove`)를 지정합니다.
    - `.id_table`: 이 드라이버가 지원하는 장치 목록(`i2c_clcd_id`)을 가리킵니다. 이 테이블 안에 `compatible` 문자열 "seong,i2c-clcd"가 포함되어 있습니다.

- **`i2c_clcd_probe(struct i2c_client *client, ...)` 함수**:
    1. **문자 디바이스 드라이버 등록**: `alloc_chrdev_region`, `cdev_init`, `cdev_add` 함수를 순서대로 호출하여 사용자 공간과 통신할 수 있는 문자 디바이스(`char device`)를 생성합니다.
    2. **디바이스 파일 생성**: `class_create`와 `device_create`를 통해 `/dev/i2c_clcd`와 같은 디바이스 노드(파일)를 자동으로 생성하여, 사용자가 파일 I/O 방식으로 드라이버에 접근할 수 있도록 합니다.
    3. **I2C 클라이언트 저장**: `i2c_set_clientdata`를 사용해 `client` 포인터를 저장하여 `write`와 같은 다른 함수에서 I2C 통신을 할 수 있도록 준비합니다.

- **`file_operations` 구조체 (`i2c_clcd_fops`)**:
    - 사용자가 `/dev/i2c_clcd` 파일을 `open`, `release`, `write` 할 때 각각 어떤 함수를 호출할지 정의합니다.
    - **`i2c_clcd_write(..., const char *buf, ...)`**: 이 드라이버의 핵심 기능입니다. 사용자가 애플리케이션에서 `write()`를 호출하면 이 함수가 실행됩니다.
        1. `copy_from_user()`: 사용자 공간 애플리케이션이 전달한 데이터를 커널 공간으로 안전하게 복사합니다.
        2. `i2c_master_send()`: 복사된 데이터를 I2C 버스를 통해 CLCD 장치(주소: 0x27)로 전송합니다. 이 함수가 실제 하드웨어 통신을 수행합니다.

- **`module_init` / `module_exit`**:
    - `i2c_clcd_init`: 모듈이 로드될 때(`insmod`) `i2c_add_driver`를 호출하여 시스템에 위에서 정의한 `i2c_clcd_driver`를 등록합니다.
    - `i2c_clcd_exit`: 모듈이 제거될 때(`rmmod`) `i2c_del_driver`를 호출하여 드라이버를 시스템에서 등록 해제합니다.

## 2. 사용자 애플리케이션

### 2.1. 테스트 애플리케이션 (`driver_app/i2c_clcd_app.c`)

이 프로그램은 사용자 공간에서 실행되며, 커널에 로드된 `i2c_clcd_dev` 드라이버를 통해 CLCD에 텍스트를 출력하는 간단한 테스트용 유틸리티입니다.

**주요 코드 분석:**

- **`main(int argc, char **argv)`**:
    1. **인자 확인**: 프로그램 실행 시 CLCD에 출력할 텍스트를 인자로 받았는지 확인합니다. (`argc < 2`)
    2. **디바이스 파일 열기**: `open("/dev/i2c_clcd", O_WRONLY)`를 호출하여 드라이버가 생성한 디바이스 파일을 쓰기 모드로 엽니다. 이 과정에서 커널의 `i2c_clcd_open` 함수가 호출됩니다.
    3. **데이터 쓰기**: `write(fd, argv[1], strlen(argv[1]))`를 호출하여 명령행 인자로 받은 문자열을 디바이스 파일에 씁니다. 이 때 커널의 `i2c_clcd_write` 함수가 실행되어 I2C 통신으로 CLCD에 데이터가 전송됩니다.
    4. **파일 닫기**: `close(fd)`를 호출하여 디바이스 파일을 닫습니다.

`driver_app/Makefile`은 `gcc`를 사용하여 이 소스 코드를 컴파일하여 `i2c_clcd_app`이라는 실행 파일을 생성하고, 동시에 커널 소스 트리와 연계하여 `i2c_clcd_dev.ko` 커널 모듈을 빌드합니다.

## 3. 빌드 및 사용법

1.  **디바이스 트리 오버레이 컴파일**:
    ```bash
    cd device_tree
    make
    # 생성된 clcd.dtbo 파일을 /boot/overlays/ 와 같은 경로에 복사
    # /boot/config.txt 에 dtoverlay=clcd 추가 후 재부팅 또는 dtoverlay 명령어로 로드
    ```

2.  **드라이버 및 애플리케이션 빌드**:
    ```bash
    cd driver_app
    make
    # i2c_clcd_dev.ko (커널 모듈)와 i2c_clcd_app (실행 파일)이 생성됨
    ```

3.  **실행**:
    ```bash
    # 1. 커널 모듈 로드
    sudo insmod i2c_clcd_dev.ko

    # 2. 애플리케이션으로 CLCD에 텍스트 출력
    ./i2c_clcd_app "Hello World"
    ```

4.  **종료**:
    ```bash
    # 커널 모듈 제거
    sudo rmmod i2c_clcd_dev
    ```
