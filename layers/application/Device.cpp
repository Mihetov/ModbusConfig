Device::Device(uint8_t slaveId,
               std::unique_ptr<ITransport> transport,
               std::unique_ptr<IModbusCodec> codec)
    : slaveId_(slaveId),
    transport_(std::move(transport)),
    codec_(std::move(codec))
{
    transport_->setReceiveCallback(
        [this](std::vector<uint8_t> data)
        {
            handleIncoming(std::move(data));
        });
}
void Device::readHoldingRegisters(uint16_t address, uint16_t quantity)
{
    ModbusRequest req;
    req.slaveId = slaveId_;
    req.functionCode = 0x03;
    req.address = address;
    req.quantity = quantity;

    auto frame = codec_->encode(req);
    transport_->send(frame);
}
void Device::handleIncoming(std::vector<uint8_t> data)
{
    auto response = codec_->decode(data);

    if (response.isException)
    {
        // обработка ошибки
        return;
    }

    // передать данные выше (в будущем в API)
}
