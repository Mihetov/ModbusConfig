# ModbusConfig

Системное консольное приложение для обработки JSON-команд и выполнения Modbus-операций.

## Архитектура

Проект разделён на 4 слоя:

1. **API** (`core/api`) — принимает JSON-запрос из консоли и возвращает JSON-ответ.
2. **Application** (`core/application`) — валидирует входные данные и оркестрирует сценарии.
3. **Protocol** (`core/protocol`) — представляет Modbus-команды/результаты и маршрутизирует вызов транспорта.
4. **Transport** (`infrastructure/transport`) — исполняет Modbus-операции.

## Текущие транспорты

- `InMemoryModbusTransport` — тестовый транспорт (регистры хранятся в памяти процесса).
- `MazurelModbusTransport` — адаптер-заглушка для `Mazurel/Modbus`.
  > В текущем окружении GitHub недоступен, поэтому библиотека не подтянута автоматически.

## Формат запроса

Одна строка JSON = один запрос.

### Команды

- `read_holding_registers`
- `write_single_register`
- `write_multiple_registers`

### Примеры

```json
{"requestId":"1","command":"write_single_register","unitId":1,"address":10,"value":777}
{"requestId":"2","command":"read_holding_registers","unitId":1,"address":10,"count":1}
{"requestId":"3","command":"write_multiple_registers","unitId":1,"address":20,"values":[1,2,3]}
```

### Ответ

Успех:

```json
{"requestId":"2","status":"ok","data":{"values":[777]}}
```

Ошибка:

```json
{"requestId":"2","status":"error","error":"Field 'count' must be > 0"}
```

## Сборка

```bash
cmake -S . -B build
cmake --build build
```

## Запуск

```bash
echo '{"requestId":"1","command":"write_single_register","address":5,"value":42}' | ./build/app/ModbusConfig
```
