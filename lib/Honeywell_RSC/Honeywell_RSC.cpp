#include "Honeywell_RSC.h"

Honeywell_RSC::Honeywell_RSC(int drdy_pin, int cs_ee_pin, int cs_adc_pin) {
  _drdy_pin = drdy_pin;
  _cs_ee_pin = cs_ee_pin;
  _cs_adc_pin = cs_adc_pin;

  pinMode(_drdy_pin, INPUT);
  pinMode(_cs_ee_pin, OUTPUT);
  pinMode(_cs_adc_pin, OUTPUT);

  // deselect both EEPROM and ADC
  digitalWrite(_cs_ee_pin, HIGH);
  digitalWrite(_cs_adc_pin, HIGH);
}

void Honeywell_RSC::init() {
  // read and store constants from EEPROM
  get_catalog_listing();
  get_serial_number();
  get_pressure_range();
  get_pressure_minimum();
  get_pressure_unit();
  get_pressure_type();

  // setup ADC
  get_initial_adc_values(adc_init_values);
  setup_adc(adc_init_values);

  get_coefficients();

  set_data_rate(N_DR_20_SPS);
  set_mode(NORMAL_MODE);
  delay(5);

}

void Honeywell_RSC::select_eeprom() {
  // make sure CS_ADC is not active
  digitalWrite(_cs_adc_pin, HIGH);

  // enable CS_EE
  digitalWrite(_cs_ee_pin, LOW);

  // the EEPROM interface operates in SPI mode 0 (CPOL = 0, CPHA = 0) or mode 3 (CPOL = 1, CPHA = 1)
  SPI.beginTransaction(SPISettings(125000, MSBFIRST, SPI_MODE0));
}

void Honeywell_RSC::deselect_eeprom() {
  SPI.endTransaction();
  digitalWrite(_cs_ee_pin, HIGH);
}

void Honeywell_RSC::select_adc() {
  // make sure CS_EE is not active
  digitalWrite(_cs_ee_pin, HIGH);

  // enable CS_ADC
  digitalWrite(_cs_adc_pin, LOW);

  // the ADC interface operates in SPI mode 1 (CPOL = 0, CPHA = 1)
  SPI.beginTransaction(SPISettings(125000, MSBFIRST, SPI_MODE1));
}

void Honeywell_RSC::deselect_adc() {
  SPI.endTransaction();
  digitalWrite(_cs_adc_pin, HIGH);
}

//////////////////// EEPROM read ////////////////////

void Honeywell_RSC::eeprom_read(uint16_t address, uint8_t num_bytes, uint8_t *data) {
  // generate command (refer to sensor datasheet section 2.2)
  uint8_t command[2] = {0};
  command[0] = RSC_READ_EEPROM_INSTRUCTION | ((address & RSC_EEPROM_ADDRESS_9TH_BIT_MASK) >> 5);
  command[1] = address & 0xFF;

  // send command
  // select EEPROM
  select_eeprom();

  SPI.transfer(command[0]);
  SPI.transfer(command[1]);

  // receive results
  // - results are transmitted back after the last bit of the command is sent
  // - to get results, just transfer dummy data, as subsequent bytes will not used by sensor
  for (int i = 0; i < num_bytes; i++) {
    data[i] = SPI.transfer(0x00);
  }

  // deselect EEPROM
  // - after command is sent, the sensor will keep sending bytes from EEPROM,
  //   in ascending order of address. Resetting the CS_EE pin at the end of
  //   the function means that when reading from EEPROM next time, the result
  //   would start at the correct address.
  deselect_eeprom();
}

void Honeywell_RSC::get_catalog_listing() {
  eeprom_read(RSC_CATALOG_LISTING_MSB, RSC_SENSOR_NAME_LEN, _catalog_listing);
}

void Honeywell_RSC::get_serial_number() {
  eeprom_read(RSC_SERIAL_NO_YYYY_MSB, RSC_SENSOR_NUMBER_LEN, (uint8_t*)_serial_number);
}

void Honeywell_RSC::get_pressure_range() {
  uint8_t buf[RSC_PRESSURE_RANGE_LEN];
  eeprom_read(RSC_PRESSURE_RANGE_LSB, RSC_PRESSURE_RANGE_LEN, buf);
  // convert byte array to float (buf[0] is LSB)
  memcpy(&_pressure_range, &buf, sizeof(_pressure_range));
}

void Honeywell_RSC::get_pressure_minimum() {
  uint8_t buf[RSC_PRESSURE_MINIMUM_LEN];
  eeprom_read(RSC_PRESSURE_MINIMUM_LSB, RSC_PRESSURE_MINIMUM_LEN, buf);
  // convert byte array to float (buf[0] is LSB)
  memcpy(&_pressure_minimum, &buf, sizeof(_pressure_minimum));
}

void Honeywell_RSC::get_pressure_unit() {
  char buf[RSC_PRESSURE_UNIT_LEN] = {0};
  eeprom_read(RSC_PRESSURE_UNIT_MSB, RSC_PRESSURE_UNIT_LEN, buf);
  buf[RSC_PRESSURE_UNIT_LEN - 1] = '\0';
  if (buf[RSC_PRESSURE_UNIT_LEN - 2] == '2') {
    _pressure_unit = INH2O;
    _pressure_unit_name = "inH2O";
  } else if (buf[RSC_PRESSURE_UNIT_LEN - 2] == 'a') {
    if (buf[RSC_PRESSURE_UNIT_LEN - 4] == 'K') {
      _pressure_unit = KPASCAL;
      _pressure_unit_name = "kilopascal";
    } else if (buf[RSC_PRESSURE_UNIT_LEN - 4] == 'M') {
      _pressure_unit = MPASCAL;
      _pressure_unit_name = "megapascal";
    } else {
      _pressure_unit = PASCAL;
      _pressure_unit_name = "pascal";
    }
  } else if (buf[RSC_PRESSURE_UNIT_LEN - 2] == 'r') {
    if (buf[RSC_PRESSURE_UNIT_LEN - 5] == 'm') {
      _pressure_unit = mBAR;
      _pressure_unit_name = "millibar";
    } else {
      _pressure_unit = BAR;
      _pressure_unit_name = "bar";
    }
  } else if (buf[RSC_PRESSURE_UNIT_LEN - 2] == 'i') {
    _pressure_unit = PSI;
    _pressure_unit_name = "psi";
  }
}

void Honeywell_RSC::get_pressure_type() {
  char buf[RSC_SENSOR_TYPE_LEN];
  eeprom_read(RSC_PRESSURE_REFERENCE, RSC_SENSOR_TYPE_LEN, buf);
  switch (buf[0]) {
    case 'D':
      _pressure_type = DIFFERENTIAL;
      _pressure_type_name = "differential";
      break;
    case 'A':
      _pressure_type = ABSOLUTE;
      _pressure_type_name = "absolute";
      break;
    case 'G':
      _pressure_type = GAUGE;
      _pressure_type_name = "gauge";
      break;
    default:
      _pressure_type = DIFFERENTIAL;
      _pressure_type_name = "differential";
  }
}

void Honeywell_RSC::get_coefficients() {
  int base_address = RSC_OFFSET_COEFFICIENT_0_LSB;
  uint8_t buf[4] = {0}; // Preallocates 4 bytes of data for the coefficient. Each coefficient is 4 bytes.
  int i, j = 0;

  // coeff matrix structure
  // _coeff_matrix[i][j]
  //  i\j   0                     1                     2                     3
  //  0   OffsetCoefficient0    OffsetCoefficient1    OffsetCoefficient2    OffsetCoefficient3
  //  1   SpanCoefficient0      SpanCoefficient1      SpanCoefficient2      SpanCoefficient3
  //  2   ShapeCoefficient0     ShapeCoefficient1     ShapeCoefficient2     ShapeCoefficient3

  // storing all the coefficients
  for (i = 0; i < RSC_COEFF_T_ROW_NO; i++) {
    for (j = 0; j < RSC_COEFF_T_COL_NO; j++) {
      // 80 is the number of bytes that separate the beginning
      // of the address spaces of all the 3 coefficient groups
      // refer the datasheet for more info
      base_address = RSC_OFFSET_COEFFICIENT_0_LSB + i * 80 + j * 4;
      eeprom_read(base_address, 4, buf);
      memcpy(&_coeff_matrix[i][j], (&buf), sizeof(_coeff_matrix[i][j]));
    }
  }
}

float Honeywell_RSC::print_coefficients() {
  // Serial.print(_coeff_matrix[0][0]);
  // Serial.print('\t');
  // Serial.print(_coeff_matrix[0][1]);
  // Serial.print('\t');
  // Serial.print(_coeff_matrix[0][2]);
  // Serial.print('\t');
  // Serial.println(_coeff_matrix[0][3],8);
  // Serial.print(_coeff_matrix[1][0]);
  // Serial.print('\t');
  // Serial.print(_coeff_matrix[1][1]);
  // Serial.print('\t');
  // Serial.print(_coeff_matrix[1][2]);
  // Serial.print('\t');
  // Serial.println(_coeff_matrix[1][3],8);
  // Serial.print(_coeff_matrix[2][0]);
  // Serial.print("\t\t");
  // Serial.print(_coeff_matrix[2][1]);
  // Serial.print('\t');
  // Serial.print(_coeff_matrix[2][2]);
  // Serial.print('\t');
  // Serial.println(_coeff_matrix[2][3],8);
  Serial.print("p_raw\t\t");
  Serial.println(p_raw);
  // Serial.print("p_int1_c\t");
  // Serial.println(p_int1_c);
  Serial.print("_t_raw\t\t");
  Serial.println(_t_raw);
  // for(int i = 0; i < 4; i++){
  //     Serial.print(" ");
  //     Serial.print(adc_init_values[i]);
  // }
  // Serial.println();
  return 0;
}

void Honeywell_RSC::get_initial_adc_values(uint8_t* adc_init_values) {
  eeprom_read(RSC_ADC_CONFIG_00, 1, &adc_init_values[0]);
  delay(2);
  eeprom_read(RSC_ADC_CONFIG_01, 1, &adc_init_values[1]);
  delay(2);
  eeprom_read(RSC_ADC_CONFIG_02, 1, &adc_init_values[2]);
  delay(2);
  eeprom_read(RSC_ADC_CONFIG_03, 1, &adc_init_values[3]);
  delay(2);
}

//////////////////// ADC reading functions ////////////////////

float Honeywell_RSC::get_temperature() {
  // reads temperature from ADC, stores raw value in sensor object, but returns the temperature in Celsius
  // refer to datasheet section 3.5 ADC Programming and Read Sequence – Temperature Reading
  uint8_t command[2] = {0};
  uint8_t temp_arr[3] = {0};
  // WREG byte
  command[0] = RSC_ADC_WREG
               | ((1 << 2) & RSC_ADC_REG_MASK);
  // configuration byte, which includes DataRate, Mode, Pressure/Temperature choice
  command[1] = (((_data_rate << RSC_DATA_RATE_SHIFT) & RSC_DATA_RATE_MASK)
                | ((_mode << RSC_OPERATING_MODE_SHIFT) & RSC_OPERATING_MODE_MASK)
                | (((TEMPERATURE & 0x01) << 1) | RSC_SET_BITS_MASK));
  // send command
  select_adc();
  while (digitalRead(_drdy_pin) == LOW) { }
  while (digitalRead(_drdy_pin) == HIGH) { }
  delayMicroseconds(td_CSSC/1000);
  SPI.transfer(command[0]);
  SPI.transfer(command[1]);
  SPI.transfer(0x08); // Initializes continuous conversion mode
  delayMicroseconds(td_SCCS/1000);
  deselect_adc();

  while (digitalRead(_drdy_pin) == HIGH) { }
  select_adc();
  delayMicroseconds(td_CSSC/1000);

  for (int i = 0; i < 3; i++) {
    temp_arr[i] = SPI.transfer(0x00);
  }
  delayMicroseconds(td_SCCS/1000);
  deselect_adc();

  // first 14 bits represent temperature
  // following 10 bits are random thus discarded
  _t_raw = (((int32_t)temp_arr[0] << 8) | (int32_t)temp_arr[1]) >> 2;
  temp = _t_raw * 0.03125;

  return temp;
}


void Honeywell_RSC::select_pressure() {
  // Executs WREG command to select pressure reading with the desired data rates
  
  uint8_t command[2] = {0};
  // WREG byte
  command[0] = RSC_ADC_WREG
               | ((1 << 2) & RSC_ADC_REG_MASK);
  // configuration byte, which includes DataRate, Mode, Pressure/Temperature choice
  command[1] = (((_data_rate << RSC_DATA_RATE_SHIFT) & RSC_DATA_RATE_MASK)
                | ((_mode << RSC_OPERATING_MODE_SHIFT) & RSC_OPERATING_MODE_MASK)
                | (((PRESSURE & 0x01) << 1) | RSC_SET_BITS_MASK));
  
  select_adc();
  while (digitalRead(_drdy_pin) == LOW) { }
  while (digitalRead(_drdy_pin) == HIGH) { }
  delayMicroseconds(td_CSSC/1000);
  SPI.transfer(command[0]);
  SPI.transfer(command[1]);
  SPI.transfer(0x08); // Initializes continuous conversion mode
  delayMicroseconds(td_SCCS/1000);
  deselect_adc();
}

float Honeywell_RSC::read_pressure() {
  uint8_t pres_arr[3] = {0};

  while (digitalRead(_drdy_pin) == HIGH) { }
  select_adc();
  delayMicroseconds(td_CSSC/1000);

  for (int i = 0; i < 3; i++) {
    pres_arr[i] = SPI.transfer(0x00);
  }

  delayMicroseconds(td_SCCS/1000);
  deselect_adc();

  p_raw = ((int32_t)pres_arr[0] << 24) | ((int32_t)pres_arr[1] << 16) | ((int32_t)pres_arr[2] << 8);
  p_raw /= 256; // this make sure that the sign of p_raw is the same as that of the 24-bit reading
  
  // calculate compensated pressure
  // refer to datasheet section 1.3 Compensation Mathematics
  float x = (_coeff_matrix[0][3] * _t_raw * _t_raw * _t_raw);
  float y = (_coeff_matrix[0][2] * _t_raw * _t_raw);
  float z = (_coeff_matrix[0][1] * _t_raw);
  p_int1_c = (x + y + z + _coeff_matrix[0][0]);
  float p_int1 = p_raw - (x + y + z + _coeff_matrix[0][0]);

  x = (_coeff_matrix[1][3] * _t_raw * _t_raw * _t_raw);
  y = (_coeff_matrix[1][2] * _t_raw * _t_raw);
  z = (_coeff_matrix[1][1] * _t_raw);
  float p_int2 = p_int1 / (x + y + z + _coeff_matrix[1][0]);

  x = (_coeff_matrix[2][3] * p_int2 * p_int2 * p_int2);
  y = (_coeff_matrix[2][2] * p_int2 * p_int2);
  z = (_coeff_matrix[2][1] * p_int2);
  float p_comp_fs = x + y + z + _coeff_matrix[2][0];

  float p_comp = (p_comp_fs * _pressure_range) + _pressure_minimum;

  return p_comp;
}

void Honeywell_RSC::adc_write(uint8_t reg, uint8_t num_bytes, uint8_t* data) {
  // check if the register and the number of bytes are valid
  // The number of bytes to write has to be - 1,2,3,4
  if (num_bytes <= 0 || num_bytes > 4)
    return;

  // the ADC registers are 0,1,2,3
  if (reg > 3)
    return;

  // generate command
  // the ADC REG Write (WREG) command is as follows: 0100 RRNN
  //   RR - Register Number             (0,1,2,3)
  //   NN - Number of Bytes to send - 1 (0,1,2,3)
  uint8_t command[num_bytes + 1];
  command[0] = RSC_ADC_WREG
               | ((reg << 2) & RSC_ADC_REG_MASK)
               | ((num_bytes - 1) & RSC_ADC_NUM_BYTES_MASK);

  for (int i = 0; i < num_bytes; i++) {
    command[i + 1] = data[i];
  }

  // send command
  select_adc();
  for (int i = 0; i < num_bytes + 1; i++) {
    SPI.transfer(command[i]);
  }
  deselect_adc();
}

void Honeywell_RSC::add_dr_delay() {
  float delay_duration = 0;
  // calculating delay based on the Data Rate
  switch (_data_rate) {
    case N_DR_20_SPS:
      delay_duration = MSEC_PER_SEC / 20;
      break;
    case N_DR_45_SPS:
      delay_duration = MSEC_PER_SEC / 45;
      break;
    case N_DR_90_SPS:
      delay_duration = MSEC_PER_SEC / 90;
      break;
    case N_DR_175_SPS:
      delay_duration = MSEC_PER_SEC / 175;
      break;
    case N_DR_330_SPS:
      delay_duration = MSEC_PER_SEC / 330;
      break;
    case N_DR_600_SPS:
      delay_duration = MSEC_PER_SEC / 600;
      break;
    case N_DR_1000_SPS:
      delay_duration = MSEC_PER_SEC / 1000;
      break;
    case F_DR_40_SPS:
      delay_duration = MSEC_PER_SEC / 40;
      break;
    case F_DR_90_SPS:
      delay_duration = MSEC_PER_SEC / 90;
      break;
    case F_DR_180_SPS:
      delay_duration = MSEC_PER_SEC / 180;
      break;
    case F_DR_350_SPS:
      delay_duration = MSEC_PER_SEC / 350;
      break;
    case F_DR_660_SPS:
      delay_duration = MSEC_PER_SEC / 660;
      break;
    case F_DR_1200_SPS:
      delay_duration = MSEC_PER_SEC / 1200;
      break;
    case F_DR_2000_SPS:
      delay_duration = MSEC_PER_SEC / 2000;
      break;
    default:
      delay_duration = 50;
  }
  delay((int)delay_duration + 2);
}

void Honeywell_RSC::set_data_rate(RSC_DATA_RATE dr) {
  _data_rate = dr;
  switch (dr) {
    case N_DR_20_SPS:
    case N_DR_45_SPS:
    case N_DR_90_SPS:
    case N_DR_175_SPS:
    case N_DR_330_SPS:
    case N_DR_600_SPS:
    case N_DR_1000_SPS:
      set_mode(NORMAL_MODE);
      break;
    case F_DR_40_SPS:
    case F_DR_90_SPS:
    case F_DR_180_SPS:
    case F_DR_350_SPS:
    case F_DR_660_SPS:
    case F_DR_1200_SPS:
    case F_DR_2000_SPS:
      set_mode(FAST_MODE);
      break;
    default:
      set_mode(NA_MODE);
  }
}

void Honeywell_RSC::set_mode(RSC_MODE mode) {
  RSC_MODE l_mode;

  switch (mode) {
    case NORMAL_MODE:
      if (_data_rate < N_DR_20_SPS || _data_rate > N_DR_1000_SPS) {
        Serial.println("RSC: Normal mode not supported with the current selection of data rate\n");
        Serial.println("RSC: You will see erronous readings\n");
        l_mode = NA_MODE;
      } else
        l_mode = NORMAL_MODE;
      break;
    case FAST_MODE:
      if (_data_rate < F_DR_40_SPS || _data_rate > F_DR_2000_SPS) {
        Serial.println("RSC: Fast mode not supported with the current selection of data rate\n");
        Serial.println("RSC: You will see erronous readings\n");
        l_mode = NA_MODE;
      } else
        l_mode = FAST_MODE;
      break;
    default:
      l_mode = NA_MODE;
  }
  _mode = l_mode;
}

void Honeywell_RSC::setup_adc(uint8_t* adc_init_values) {
  // refer to datasheet section 3.4 ADC Programming Sequence – Power Up
  uint8_t regs[4] = {0x40, 0x44, 0x48, 0x4C}; //WREG commands for each of the four configuration registers
  uint8_t command[4] = {adc_init_values[0], adc_init_values[1], adc_init_values[2], adc_init_values[3]};
  uint8_t commandread[4] = {0x20, 0x24, 0x28, 0x2C};
  select_adc();
  delayMicroseconds(50);
  SPI.transfer(RSC_ADC_RESET_COMMAND);
  delayMicroseconds(50+32*204768/999);
  // adc_write(0, 4, command);
  SPI.transfer(regs[0]);
  SPI.transfer(command[0]);
  SPI.transfer(regs[1]);
  SPI.transfer(command[1]);
  SPI.transfer(regs[2]);
  SPI.transfer(command[2]);
  SPI.transfer(regs[3]);
  SPI.transfer(command[3]);

  uint8_t dt[4];
  for(int i=0; i < 4; i++){
    SPI.transfer(commandread[i]);
    dt[i] = SPI.transfer(0x00);
    if( command[i] != dt[i]){
      while(1) {
        Serial.print( command[i]);
        Serial.print( " != ");
        Serial.print(dt[i]);
        Serial.print( " reg ");
        Serial.print(commandread[i]); 
        Serial.println("ADC READ ERR");
        delay(500);
        }
    }
  }
  delay(5);
  
}

