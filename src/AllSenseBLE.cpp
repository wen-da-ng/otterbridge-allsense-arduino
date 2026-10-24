// BLE client: ESP32 only (other boards use AllSenseSerial over USB).
#if defined(ESP32)

#include "AllSenseBLE.h"

#include <BLEDevice.h>
#if defined(CONFIG_BLUEDROID_ENABLED)
#include <BLE2902.h>
#endif
#include <BLEServer.h>
#include <BLEUtils.h>

namespace {
// https://docs.allsense.otterbridge.org/PROTOCOL/ ("BLE transport"). RX = phone -> board (write), TX = board -> phone (notify).
const char* kService = "a1150001-ae7a-4681-958d-74782a0d0f32";
const char* kRx = "a1150002-ae7a-4681-958d-74782a0d0f32";
const char* kTx = "a1150003-ae7a-4681-958d-74782a0d0f32";
const size_t kNotifyChunk = 20;  // fits the smallest BLE packet; the phone reassembles the byte stream

class ServerCallbacks : public BLEServerCallbacks {
public:
    explicit ServerCallbacks(AllSenseBleStream& stream) : _stream(stream) {}
    void onConnect(BLEServer*) override { _stream.setConnected(true); }
    void onDisconnect(BLEServer* server) override {
        _stream.setConnected(false);
        server->getAdvertising()->start();  // let the phone find us again
    }

private:
    AllSenseBleStream& _stream;
};

class RxCallbacks : public BLECharacteristicCallbacks {
public:
    explicit RxCallbacks(AllSenseBleStream& stream) : _stream(stream) {}
    void onWrite(BLECharacteristic* c) override { _stream.received(c->getData(), c->getLength()); }

private:
    AllSenseBleStream& _stream;
};
}  // namespace

void AllSenseBleStream::received(const uint8_t* data, size_t length) {
    portENTER_CRITICAL(&_lock);
    for (size_t i = 0; i < length; i++) {
        size_t next = (_head + 1) % kSize;
        if (next == _tail) break;  // full: drop (loop() isn't being called often enough)
        _buffer[_head] = data[i];
        _head = next;
    }
    portEXIT_CRITICAL(&_lock);
}

void AllSenseBleStream::setConnected(bool connected) {
    _connected = connected;
    if (!connected) {
        portENTER_CRITICAL(&_lock);
        _head = _tail = 0;
        portEXIT_CRITICAL(&_lock);
    }
}

int AllSenseBleStream::available() {
    portENTER_CRITICAL(&_lock);
    int n = (int)((_head + kSize - _tail) % kSize);
    portEXIT_CRITICAL(&_lock);
    return n;
}

int AllSenseBleStream::read() {
    int c = -1;
    portENTER_CRITICAL(&_lock);
    if (_tail != _head) {
        c = _buffer[_tail];
        _tail = (_tail + 1) % kSize;
    }
    portEXIT_CRITICAL(&_lock);
    return c;
}

int AllSenseBleStream::peek() {
    int c = -1;
    portENTER_CRITICAL(&_lock);
    if (_tail != _head) c = _buffer[_tail];
    portEXIT_CRITICAL(&_lock);
    return c;
}

size_t AllSenseBleStream::write(const uint8_t* data, size_t length) {
    if (!_connected || !_tx) return length;  // nobody listening: drop (hello is retried anyway)
    for (size_t off = 0; off < length; off += kNotifyChunk) {
        size_t n = length - off < kNotifyChunk ? length - off : kNotifyChunk;
        _tx->setValue(const_cast<uint8_t*>(data + off), n);
        _tx->notify();
    }
    return length;
}

void AllSenseBLE::begin(const char* boardName) {
    BLEDevice::init(boardName);
    BLEDevice::setMTU(247);  // the phone asks for 247, so a whole frame fits in one write
    _server = BLEDevice::createServer();
    _server->setCallbacks(new ServerCallbacks(_stream));
    BLEService* service = _server->createService(kService);
    BLECharacteristic* rx = service->createCharacteristic(kRx, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(new RxCallbacks(_stream));
    BLECharacteristic* tx = service->createCharacteristic(kTx, BLECharacteristic::PROPERTY_NOTIFY);
#if defined(CONFIG_BLUEDROID_ENABLED)
    tx->addDescriptor(new BLE2902());  // NimBLE (the default on ESP32-S3) adds it by itself
#endif
    _stream.setTx(tx);
    service->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(kService);
    // The 128-bit service UUID fills most of the 31-byte advert, so the name goes in the scan
    // response (set explicitly: not every BLE stack adds it by itself).
    BLEAdvertisementData scanResponse;
    scanResponse.setName(boardName);
    advertising->setScanResponseData(scanResponse);
    advertising->setScanResponse(true);
    advertising->start();

    AllSenseSerial::begin(_stream, boardName);
}

#endif  // ESP32
