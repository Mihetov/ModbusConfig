# ModbusConfig

## Описание

`ModbusConfig` — это C++ сервис для работы с Modbus-устройствами через TCP и RTU.
Проект построен по слоистой архитектуре и запускает локальный HTTP JSON-RPC сервер,
к которому подключается фронтенд.

Архитектура слоёв:
- **API Layer** → обработка HTTP POST JSON-RPC запросов.
- **Application Layer** → бизнес-логика, управление транспортом и задачами чтения/записи.
- **Protocol Layer** → преобразование JSON/DTO в Modbus кадры и обратно.
- **Transport Layer** → байтовый обмен через Boost.Asio (TCP/Serial).

## Основной функционал

- Открытие/закрытие/переключение транспорта **TCP/RTU**.
- Получение текущего статуса транспорта.
- Получение списка доступных serial-портов (Linux/Windows).
- Чтение одиночных и групповых регистров.
- Запись одиночных и групповых регистров.
- Поддержка адресов регистров в формате:
  - decimal (`100`)
  - hex-string (`0x0064`, `0xF020`)
- Возврат подробного ответа на чтение:
  - `slave_id`, `address`, `count`, `function`, `values`, `ok`.

## Используемые библиотеки

- **Boost.Asio** — транспортный слой (TCP и Serial I/O).
- **Boost.Beast** — HTTP сервер.
- **Boost.JSON** — JSON/JSON-RPC сериализация и парсинг.
- **MB (локальная библиотека проекта)** — Modbus-специфичные компоненты.
- **C++17 STL** — потоки, контейнеры, синхронизация, filesystem.

## Параметры запуска

Приложение поддерживает параметры командной строки:

### Режим и API
- `--mode <api|headless>` — режим запуска (по умолчанию `api`).
- `--bind <ip>` — адрес привязки API (по умолчанию `0.0.0.0`).
- `--api-port <port>` — порт API (по умолчанию `8080`).

### Автозапуск транспорта
- `--transport <none|tcp|rtu>` — открыть транспорт при старте (по умолчанию `none`).

#### Для TCP
- `--tcp-host <ip>` — адрес устройства (по умолчанию `127.0.0.1`).
- `--tcp-port <port>` — порт Modbus TCP (по умолчанию `502`).

#### Для RTU
- `--rtu-port <device>` — serial-порт (`/dev/ttyUSB0`, `COM3` и т.д.).
- `--rtu-baud <rate>` — скорость (`9600`, `19200`, ...).
- `--rtu-stop-bits <1|2>` — стоп-биты.

### Дополнительно
- `--verbose-modbus` — печатать Modbus-ответы в stdout.
- `--help` — показать справку.

## Примеры запуска

```bash
# API режим без автоподключения транспорта
./ModbusConfig --mode api --bind 0.0.0.0 --api-port 8080

# API режим + автоподключение TCP
./ModbusConfig --mode api --transport tcp --tcp-host 192.168.0.10 --tcp-port 502

# Headless режим + автоподключение RTU
./ModbusConfig --mode headless --transport rtu --rtu-port /dev/ttyUSB0 --rtu-baud 38400 --rtu-stop-bits 1
```

## API методы (JSON-RPC)

Сервер принимает `POST` JSON-RPC запросы.

- `ping`
- `transport.status`
- `transport.serial_ports`
- `transport.open`
- `transport.switch`
- `transport.close`
- `modbus.read`
- `modbus.read_group`
- `modbus.write`
- `modbus.write_group`

## Функции фронтенда (`ModbusFrontend.html`)

В корне проекта добавлен файл `ModbusFrontend.html` с готовой панелью управления.

Что умеет фронтенд:

1. **Управление транспортом**
   - выбор TCP/RTU вкладки;
   - open/close/switch;
   - refresh статуса подключения.

2. **Чтение Modbus**
   - одиночное чтение;
   - batch чтение;
   - логирование ответов.

3. **Запись Modbus**
   - запись одного значения;
   - запись массива значений;
   - batch запись.

4. **Импорт регистров из CSV**
   - парсинг пользовательского CSV формата;
   - отображение таблицы регистров;
   - фильтрация/поиск;
   - чтение конкретного регистра по кнопке;
   - редактирование и запись регистров;
   - последовательное чтение всех регистров с прогресс-баром и статистикой.

5. **Системные функции UI**
   - общий лог JSON-RPC;
   - ping;
   - очистка логов.

> Важно: в `ModbusFrontend.html` по умолчанию указан `API_URL = 'http://127.0.0.1:8001/'`.
> При необходимости измените URL под ваш запуск (`--bind`, `--api-port`).

## Сборка

```bash
cmake -S . -B build
cmake --build build -j
```

Если CMake сообщает об отсутствии `Boost::system` и `Boost::json`, установите соответствующие Boost dev-пакеты в окружении.
