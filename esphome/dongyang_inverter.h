#pragma once
#include "esphome.h"
#include <vector>

// 동양(Dongyang) / 다스텍(Dastech) 태양광 인버터 RS485 통신 커스텀 컴포넌트
// PollingComponent를 상속받아 주기적으로 실행되며, UARTDevice를 상속받아 RS-485 시리얼 통신을 수행합니다.
class DongyangInverter : public PollingComponent, public UARTDevice {
public:
  // 1번 및 2번 인버터의 국번 ID (기본값: 1번 0x02, 2번 0x61. 가정 환경에 따라 set_inv1_id 등으로 변경 가능)
  uint8_t inv1_id = 0x02;
  uint8_t inv2_id = 0x61;

  // 1번 및 2번 인버터의 측정 데이터를 저장할 센서 포인터 배열 (각 10개)
  // [0]: DC Voltage (V)
  // [1]: DC Current (A)
  // [2]: DC Power (W)
  // [3]: AC Voltage (V)
  // [4]: AC Current (A)
  // [5]: AC Power (W)
  // [6]: Temperature (°C)
  // [7]: Energy Today (kWh)
  // [8]: Energy Total (kWh)
  // [9]: Frequency (Hz)
  Sensor *sensors_1[10];
  Sensor *sensors_2[10];

  // 1번 및 2번 인버터의 통신 품질 진단 센서 (0: 성공 횟수, 1: 에러 횟수, 2: 타임아웃 횟수)
  Sensor *diag_1[3];
  Sensor *diag_2[3];

  uint32_t count_succ_1 = 0;
  uint32_t count_err_1 = 0;
  uint32_t count_tout_1 = 0;

  uint32_t count_succ_2 = 0;
  uint32_t count_err_2 = 0;
  uint32_t count_tout_2 = 0;

  // 인버터 대수 (기본 1대, set_inv2 호출 시 2대로 자동 확장)
  int num_inverters = 1;

  // 생성자: 상위 설정에서 전달받은 UART 버스 컴포넌트를 연결합니다.
  DongyangInverter(UARTComponent *parent) : PollingComponent(5000), UARTDevice(parent) {
    for (int i = 0; i < 10; i++) {
      sensors_1[i] = nullptr;
      sensors_2[i] = nullptr;
    }
    for (int i = 0; i < 3; i++) {
      diag_1[i] = nullptr;
      diag_2[i] = nullptr;
    }
  }

  // 인버터 ID 설정 (기본값: 1번 0x02, 2번 0x61)
  void set_inv1_id(uint8_t id) { inv1_id = id; }
  void set_inv2_id(uint8_t id) { inv2_id = id; }

  // 1번 인버터 센서 등록
  void set_inv1(Sensor* v, Sensor* a, Sensor* pdc, Sensor* vac, Sensor* aac, Sensor* pac, Sensor* temp, Sensor* etoday, Sensor* etotal, Sensor* freq) {
    sensors_1[0] = v; sensors_1[1] = a; sensors_1[2] = pdc; sensors_1[3] = vac; sensors_1[4] = aac;
    sensors_1[5] = pac; sensors_1[6] = temp; sensors_1[7] = etoday; sensors_1[8] = etotal; sensors_1[9] = freq;
  }

  // 2번 인버터 센서 등록 (2대 병렬 환경 시 호출)
  void set_inv2(Sensor* v, Sensor* a, Sensor* pdc, Sensor* vac, Sensor* aac, Sensor* pac, Sensor* temp, Sensor* etoday, Sensor* etotal, Sensor* freq) {
    sensors_2[0] = v; sensors_2[1] = a; sensors_2[2] = pdc; sensors_2[3] = vac; sensors_2[4] = aac;
    sensors_2[5] = pac; sensors_2[6] = temp; sensors_2[7] = etoday; sensors_2[8] = etotal; sensors_2[9] = freq;
    num_inverters = 2;
  }

  // 1번 인버터 진단 센서 등록
  void set_diag1(Sensor* succ, Sensor* err, Sensor* tout) {
    diag_1[0] = succ; diag_1[1] = err; diag_1[2] = tout;
  }

  // 2번 인버터 진단 센서 등록
  void set_diag2(Sensor* succ, Sensor* err, Sensor* tout) {
    diag_2[0] = succ; diag_2[1] = err; diag_2[2] = tout;
  }

  // NVS 플래시 메모리 전역 변수 포인터 (야간 전원 오프 및 재부팅 시 발전량 복구용)
  float *p_i1_etod = nullptr;
  float *p_i1_etot = nullptr;
  float *p_i2_etod = nullptr;
  float *p_i2_etot = nullptr;

  void set_storage(float *i1_etod, float *i1_etot, float *i2_etod = nullptr, float *i2_etot = nullptr) {
    p_i1_etod = i1_etod;
    p_i1_etot = i1_etot;
    p_i2_etod = i2_etod;
    p_i2_etot = i2_etot;
  }

  // 통신 상태 머신 변수 (0: 유휴, 1: 1번 인버터 통신중, 2: 2번 인버터 통신중, 3: 버스 안정화 1초 대기중)
  int state = 0;
  uint32_t wait_start = 0;
  std::vector<uint8_t> rx_buffer;

  // 연속 타임아웃 카운터 (일몰 슬립 판별용)
  int timeout_cnt_1 = 0;
  int timeout_cnt_2 = 0;

  // 하드웨어 UART 수신 FIFO 비우기 (직전 잔류 노이즈 제거)
  void flush_rx() {
    while (available()) {
      read();
    }
  }

  // Polling 주기(yaml에서 설정된 interval)마다 실행되는 데이터 폴링 시작 함수
  void update() override {
    ESP_LOGD("Dongyang", "--- [RS485] 1번 인버터(ID: 0x%02X) 데이터 요청 전송 ---", inv1_id);
    flush_rx();
    rx_buffer.clear();
    state = 1;
    
    // 1번 인버터 질의 프레임: 7바이트 [0x0A, 0x96, ID, 0x54, 0x18, 0x05, (ID + 0x54 + 0x18) & 0xFF]
    uint8_t chk1 = (uint8_t)(inv1_id + 0x54 + 0x18);
    uint8_t req1[] = {0x0A, 0x96, inv1_id, 0x54, 0x18, 0x05, chk1};
    write_array(req1, 7);
    wait_start = millis();
  }

  // 통신 두절 시(일몰 야간 슬립) 발전 출력 및 전압/전류를 0으로 안전하게 초기화
  void publish_zero(int inv_num) {
    Sensor **s = (inv_num == 1) ? sensors_1 : sensors_2;
    if (s[0] == nullptr) return;
    if (!s[0]->has_state() || s[0]->state != 0.0) s[0]->publish_state(0.0); // DC V
    if (!s[1]->has_state() || s[1]->state != 0.0) s[1]->publish_state(0.0); // DC A
    if (!s[2]->has_state() || s[2]->state != 0.0) s[2]->publish_state(0.0); // DC W
    if (!s[3]->has_state() || s[3]->state != 0.0) s[3]->publish_state(0.0); // AC V
    if (!s[4]->has_state() || s[4]->state != 0.0) s[4]->publish_state(0.0); // AC A
    if (!s[5]->has_state() || s[5]->state != 0.0) s[5]->publish_state(0.0); // AC W
    if (!s[9]->has_state() || s[9]->state != 0.0) s[9]->publish_state(0.0); // Freq
    // ※ 발전량(etoday, etotal)은 0으로 만들지 않고 직전 누적값을 유지합니다.
  }

  // 통신 진단 통계 엔티티 업데이트
  void publish_diag(int inv_num) {
    Sensor **d = (inv_num == 1) ? diag_1 : diag_2;
    uint32_t s = (inv_num == 1) ? count_succ_1 : count_succ_2;
    uint32_t e = (inv_num == 1) ? count_err_1 : count_err_2;
    uint32_t t = (inv_num == 1) ? count_tout_1 : count_tout_2;
    if (d[0] != nullptr) d[0]->publish_state(s);
    if (d[1] != nullptr) d[1]->publish_state(e);
    if (d[2] != nullptr) d[2]->publish_state(t);
  }

  // ESP32 메인 루프 (논블로킹 수신 및 슬라이딩 윈도우 파싱)
  void loop() override {
    // [상태 3] 1번 인버터 통신 완료 후 RS-485 버스 신호 충돌 방지를 위한 1초 대기
    if (state == 3) {
      if (millis() - wait_start >= 1000) {
        flush_rx();
        rx_buffer.clear();
        state = 2;
        ESP_LOGD("Dongyang", "--- [RS485] 2번 인버터(ID: 0x%02X) 데이터 요청 전송 ---", inv2_id);
        uint8_t chk2 = (uint8_t)(inv2_id + 0x54 + 0x18);
        uint8_t req2[] = {0x0A, 0x96, inv2_id, 0x54, 0x18, 0x05, chk2};
        write_array(req2, 7);
        wait_start = millis();
      }
      return;
    }

    // [상태 1 & 2] 응답 데이터 수신 처리
    if (state == 1 || state == 2) {
      while (available()) {
        uint8_t b = read();
        ESP_LOGD("Dongyang", "RX: 0x%02X", b);
        rx_buffer.push_back(b);
      }

      // [슬라이딩 윈도우 동기화] 0xB1 0xB7 헤더가 버퍼 맨 앞으로 올 때까지 선두 바이트 제거
      while (rx_buffer.size() >= 2 && (rx_buffer[0] != 0xB1 || rx_buffer[1] != 0xB7)) {
        rx_buffer.erase(rx_buffer.begin());
      }
      if (rx_buffer.size() == 1 && rx_buffer[0] != 0xB1) {
        rx_buffer.clear();
      }
      // 통신선 잡음으로 인한 비정상 버퍼 팽창 방지 안전장치
      if (rx_buffer.size() > 128) {
        rx_buffer.clear();
      }

      // 32바이트 완전한 프레임이 수신되었을 때
      if (rx_buffer.size() >= 32) {
        bool ok = parse_data(state);
        
        if (ok) {
          if (state == 1) {
            timeout_cnt_1 = 0;
            count_succ_1++;
          } else {
            timeout_cnt_2 = 0;
            count_succ_2++;
          }
        } else {
          if (state == 1) count_err_1++;
          else count_err_2++;
        }
        publish_diag(state);
        
        rx_buffer.clear();

        // 다음 상태 천이
        if (state == 1 && num_inverters >= 2) {
          state = 3; // 2대 모드일 경우 1초 대기 상태로 이동
          wait_start = millis();
        } else {
          state = 0; // 1대 모드이거나 2번 인버터까지 완료 시 유휴 상태로 복귀
        }
      } 
      // 1.5초 이내 응답이 없을 경우 타임아웃 처리 (인버터 오프/단선 등)
      else if (millis() - wait_start > 1500) {
        ESP_LOGW("Dongyang", "인버터 %d 응답 타임아웃 발생", state);
        
        if (state == 1) {
          timeout_cnt_1++;
          count_tout_1++;
          if (timeout_cnt_1 > 12) publish_zero(1);
        } else if (state == 2) {
          timeout_cnt_2++;
          count_tout_2++;
          if (timeout_cnt_2 > 12) publish_zero(2);
        }
        publish_diag(state);
        
        rx_buffer.clear();
        if (state == 1 && num_inverters >= 2) {
          state = 3;
          wait_start = millis();
        } else {
          state = 0;
        }
      }
    }
  }

  // 32바이트 응답 패킷 검증 및 센서 값 계산/발행
  bool parse_data(int inv_num) {
    if (rx_buffer.size() < 32) return false;

    // 1. 패킷 헤더 검증
    if (rx_buffer[0] != 0xB1 || rx_buffer[1] != 0xB7) {
      ESP_LOGE("Dongyang", "잘못된 헤더 수신 Inv %d: 0x%02X 0x%02X", inv_num, rx_buffer[0], rx_buffer[1]);
      return false;
    }

    // 2. 응답 인버터 ID 일치 검증
    uint8_t expected_id = (inv_num == 1) ? inv1_id : inv2_id;
    if (rx_buffer[2] != expected_id) {
      ESP_LOGW("Dongyang", "인버터 ID 불일치! 기대값: 0x%02X, 실제수신: 0x%02X", expected_id, rx_buffer[2]);
      return false;
    }

    // 3. XOR 체크섬 검증 (0~30번 바이트의 XOR 합이 31번 바이트와 일치해야 함)
    uint8_t sum = 0;
    for (int i = 0; i < 31; i++) {
      sum ^= rx_buffer[i];
    }
    if (sum != rx_buffer[31]) {
      ESP_LOGE("Dongyang", "체크섬 오류 Inv %d (계산값: 0x%02X, 패킷값: 0x%02X)", inv_num, sum, rx_buffer[31]);
      return false;
    }

    // 4. Little-Endian 원시 데이터 환산
    float v_dc = ((rx_buffer[4] << 8) | rx_buffer[3]) / 10.0;
    float a_dc = ((rx_buffer[6] << 8) | rx_buffer[5]) / 10.0;
    float p_dc = v_dc * a_dc;

    float v_ac = ((rx_buffer[10] << 8) | rx_buffer[9]) / 10.0;
    float a_ac = ((rx_buffer[12] << 8) | rx_buffer[11]) / 10.0;
    float p_ac = v_ac * a_ac;

    float temp = ((rx_buffer[14] << 8) | rx_buffer[13]) / 10.0;
    float e_today = ((rx_buffer[16] << 8) | rx_buffer[15]) / 100.0;
    
    // 누적 발전량은 3바이트 정수(kWh)
    uint32_t e_total = rx_buffer[17] | (rx_buffer[18] << 8) | (rx_buffer[19] << 16);
    float freq = ((rx_buffer[26] << 8) | rx_buffer[25]) / 10.0;

    // 5. 홈어시스턴트 센서 엔티티로 발행
    Sensor **s = (inv_num == 1) ? sensors_1 : sensors_2;
    if (s[0] != nullptr) {
      s[0]->publish_state(v_dc);
      s[1]->publish_state(a_dc);
      s[2]->publish_state(p_dc);
      s[3]->publish_state(v_ac);
      s[4]->publish_state(a_ac);
      s[5]->publish_state(p_ac);
      s[6]->publish_state(temp);
      s[7]->publish_state(e_today);
      s[8]->publish_state(e_total);
      s[9]->publish_state(freq);
    }

    // 6. NVS 영구 저장소에 실시간 반영 (재부팅 및 야간 전원 오프 대비)
    if (inv_num == 1) {
      if (p_i1_etod != nullptr && e_today >= 0.0f) *p_i1_etod = e_today;
      if (p_i1_etot != nullptr && e_total > 0) *p_i1_etot = (float)e_total;
    } else {
      if (p_i2_etod != nullptr && e_today >= 0.0f) *p_i2_etod = e_today;
      if (p_i2_etot != nullptr && e_total > 0) *p_i2_etot = (float)e_total;
    }

    return true;
  }
};

DongyangInverter *my_inv_global = nullptr;
