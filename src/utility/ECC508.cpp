#include <Arduino.h>

#include "ECC508.h"

ECC508Class::ECC508Class(TwoWire& wire, uint8_t address) :
  _wire(&wire),
  _address(address)
{
}

ECC508Class::~ECC508Class()
{
}

int ECC508Class::begin()
{
  _wire->begin();
  _wire->setClock(100000);

  if (version() != 0x500000) {
    return 0;
  }

  return 1;
}

void ECC508Class::end()
{
  _wire->end();
}

String ECC508Class::serialNumber()
{
  String result = (char*)NULL;
  byte sn[12];

  if (!read(0, 0, &sn[0], 4)) {
    return result;
  }

  if (!read(0, 2, &sn[4], 4)) {
    return result;
  }

  if (!read(0, 3, &sn[8], 4)) {
    return result;
  }

  result.reserve(18);

  for (int i = 0; i < 8; i++) {
    byte b = sn[i];

    if (b < 16) {
      result += "0";
    }
    result += String(b, HEX);
  }

  return result;
}

int ECC508Class::random(byte data[], size_t length)
{
  if (!wakeup()) {
    return 0;
  }

  while (length) {
    if (!sendCommand(0x1b, 0x00, 0x0000)) {
      return 0;
    }

    delay(23);

    byte response[32];

    if (!receiveResponse(response, sizeof(response))) {
      return 0;
    }

    int copyLength = min(32, length);
    memcpy(data, response, copyLength);

    length -= copyLength;
    data += copyLength;
  }

  delay(1);

  idle();

  return 1;
}

int ECC508Class::generatePrivateKey(int slot, byte publicKey[])
{
  if (!wakeup()) {
    return 0;
  }

  if (!sendCommand(0x40, 0x04, slot)) {
    return 0;
  }

  delay(115);

  if (!receiveResponse(publicKey, 64)) {
    return 0;
  }

  delay(1);

  idle();

  return 1;
}

int ECC508Class::generatePublicKey(int slot, byte publicKey[])
{
  if (!wakeup()) {
    return 0;
  }

  if (!sendCommand(0x40, 0x00, slot)) {
    return 0;
  }

  delay(115);

  if (!receiveResponse(publicKey, 64)) {
    return 0;
  }

  delay(1);

  idle();

  return 1;
}

int ECC508Class::ecdsaVerify(const byte message[], const byte signature[], const byte pubkey[])
{
  if (!challenge(message)) {
    return 0;
  }

  if (!verify(signature, pubkey)) {
    return 0;
  }

  return 1;
}

int ECC508Class::ecSign(int slot, const byte message[], byte signature[])
{
  byte rand[32];

  if (!random(rand, sizeof(rand))) {
    return 0;
  }

  if (!challenge(message)) {
    return 0;
  }

  if (!sign(slot, signature)) {
    return 0;
  }

  return 1;
}

int ECC508Class::locked()
{
  byte config[4];

  if (!read(0, 0x15, config, sizeof(config))) {
    return 0;
  }

  if (config[2] == 0x00 && config[3] == 0x00) {
    return 1; // locked
  }

  return 0;
}

int ECC508Class::writeConfiguration(const byte data[])
{
  // skip first 16 bytes, they are not writable
  for (int i = 16; i < 128; i += 4) {
    if (i == 84) {
      // not writable
      continue;
    }

    if (!write(0, i / 4, &data[i], 4)) {
      return 0;
    }
  }

  return 1;
}

int ECC508Class::readConfiguration(byte data[])
{
  for (int i = 0; i < 128; i += 32) {
    if (!read(0, i / 4, &data[i], 32)) {
      return 0;
    }
  }

  return 1;
}

int ECC508Class::lock()
{
  // lock config
  if (!lock(0)) {
    return 0;
  }

  // lock data and OTP
  if (!lock(1)) {
    return 0;
  }

  return 1;
}

int ECC508Class::wakeup()
{
  _wire->beginTransmission(0x00);
  _wire->endTransmission();

  delayMicroseconds(800);

  byte response;

  if (!receiveResponse(&response, sizeof(response)) || response != 0x11) {
    return 0;
  }

  return 1;
}

int ECC508Class::sleep()
{
  _wire->beginTransmission(_address);
  _wire->write(0x01);

  if (_wire->endTransmission() != 0) {
    return 0;
  }

  return 1;
}

int ECC508Class::idle()
{
  _wire->beginTransmission(_address);
  _wire->write(0x02);

  if (_wire->endTransmission() != 0) {
    return 0;
  }

  return 1;
}

int ECC508Class::version()
{
  uint32_t version = 0;

  if (!wakeup()) {
    return 0;
  }

  if (!sendCommand(0x30, 0x00, 0x0000)) {
    return 0;
  }

  delay(1);

  if (!receiveResponse(&version, sizeof(version))) {
    return 0;
  }

  delay(1);
  idle();

  return version;
}

int ECC508Class::challenge(const byte message[])
{
  uint8_t status;

  if (!wakeup()) {
    return 0;
  }

  // Nounce, pass through
  if (!sendCommand(0x16, 0x03, 0x0000, message, 32)) {
    return 0;
  }

  delay(7);

  if (!receiveResponse(&status, sizeof(status))) {
    return 0;
  }

  delay(1);
  idle();

  if (status != 0) {
    return 0;
  }

  return 1;
}

int ECC508Class::verify(const byte signature[], const byte pubkey[])
{
  uint8_t status;

  if (!wakeup()) {
    return 0;
  }

  byte data[128];
  memcpy(&data[0], signature, 64);
  memcpy(&data[64], pubkey, 64);

  // Verify, external, P256
  if (!sendCommand(0x45, 0x02, 0x0004, data, sizeof(data))) {
    return 0;
  }

  delay(58);

  if (!receiveResponse(&status, sizeof(status))) {
    return 0;
  }

  delay(1);
  idle();

  if (status != 0) {
    return 0;
  }

  return 1;
}

int ECC508Class::sign(int slot, byte signature[])
{
  if (!wakeup()) {
    return 0;
  }

  if (!sendCommand(0x41, 0x80, slot)) {
    return 0;
  }

  delay(50);

  if (!receiveResponse(signature, 64)) {
    return 0;
  }

  delay(1);
  idle();

  return 1;
}

int ECC508Class::read(int zone, int address, byte buffer[], int length)
{
  if (!wakeup()) {
    return 0;
  }

  if (length != 4 && length != 32) {
    return 0;
  }

  if (length == 32) {
    zone |= 0x80;
  }

  if (!sendCommand(0x02, zone, address)) {
    return 0;
  }

  delay(1);

  if (!receiveResponse(buffer, length)) {
    return 0;
  }

  delay(1);
  idle();

  return length;
}

int ECC508Class::write(int zone, int address, const byte buffer[], int length)
{
  uint8_t status;

  if (!wakeup()) {
    return 0;
  }

  if (length != 4 && length != 32) {
    return 0;
  }

  if (length == 32) {
    zone |= 0x80;
  }

  if (!sendCommand(0x12, zone, address, buffer, length)) {
    return 0;
  }

  delay(26);

  if (!receiveResponse(&status, sizeof(status))) {
    return 0;
  }

  delay(1);
  idle();

  if (status != 0) {
    return 0;
  }

  return 1;
}

int ECC508Class::lock(int zone)
{
  uint8_t status;

  if (!wakeup()) {
    return 0;
  }

  if (!sendCommand(0x17, 0x80 | zone, 0x0000)) {
    return 0;
  }

  delay(32);

  if (!receiveResponse(&status, sizeof(status))) {
    return 0;
  }

  delay(1);
  idle();

  if (status != 0) {
    return 0;
  }

  return 1;
}

int ECC508Class::sendCommand(uint8_t opcode, uint8_t param1, uint16_t param2, const byte data[], size_t dataLength)
{
  byte command[8 + dataLength]; // 1 for type, 1 for length, 1 for opcode, 1 for param1, 2 for param2, 2 for crc

  command[0] = 0x03;
  command[1] = sizeof(command) - 1;
  command[2] = opcode;
  command[3] = param1;
  memcpy(&command[4], &param2, sizeof(param2));
  memcpy(&command[6], data, dataLength);

  uint16_t crc = crc16(&command[1], 8 - 3 + dataLength);
  memcpy(&command[6 + dataLength], &crc, sizeof(crc));

  if (_wire->sendTo(_address, command, 8 + dataLength) != 0) {
    return 0;
  }

  return 1;
}

int ECC508Class::receiveResponse(void* response, size_t length)
{
  int retries = 20;
  int responseSize = length + 3; // 1 for length header, 2 for CRC
  byte responseBuffer[responseSize];

  while (_wire->requestFrom(_address, responseBuffer, responseSize) != responseSize && retries--);

  // make sure length matches
  if (responseBuffer[0] != responseSize) {
    return 0;
  }

  // verify CRC
  uint16_t responseCrc = responseBuffer[length + 1] | (responseBuffer[length + 2] << 8);
  if (responseCrc != crc16(responseBuffer, responseSize - 2)) {
    return 0;
  }
  
  memcpy(response, &responseBuffer[1], length);

  return 1;
}

uint16_t ECC508Class::crc16(const byte data[], size_t length)
{
  if (data == NULL || length == 0) {
    return 0;
  }

  uint16_t crc = 0;

  while (length) {
    byte b = *data;

    for (uint8_t shift = 0x01; shift > 0x00; shift <<= 1) {
      uint8_t dataBit = (b & shift) ? 1 : 0;
      uint8_t crcBit = crc >> 15;

      crc <<= 1;
      
      if (dataBit != crcBit) {
        crc ^= 0x8005;
      }
    }

    length--;
    data++;
  }

  return crc;
}

ECC508Class ECC508(Wire, 0x60);
