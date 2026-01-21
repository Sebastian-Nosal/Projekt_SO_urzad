# API

## IPC

### Shared memory
Shared memory uses `SHM_KEY` from [config/config.h](config/config.h). Layout is defined in [config/shm.h](config/shm.h):

- `activeOfficers`: total active officers.
- `clientsInBuilding`: current number of clients inside.
- `officeOpen`: 1 = open, 0 = closed.
- `officerStatus[5]`: optional per-department status slots.

Access to shared memory is protected with the named semaphore `SEMAPHORE_NAME` from [config/config.h](config/config.h).

### Message queue
Two MQs are used:

- Petent queue: key `MQ_KEY` with `mtype = PID` of the recipient petent
- Other processes queue: key `MQ_KEY + 1` with `mtype` from `ProcessMqType` (loader/biletomat/monitoring/kasa/dyrektor)
- Urzednik queues use `DepartmentMqType` (SA/SC/KM/ML/PD) on the other-process queue

Message schema is in [config/messages.h](config/messages.h). Use `group` to select which union field is valid.

## Loader flow

1. Initializes shared memory and semaphore.
2. Spawns processes: `director`, `urzednik` (with department arg), `cashier`, `ticket_machine`.
3. Spawns `petent` processes in intervals. It only limits the number of spawned processes using current `clientsInBuilding` to avoid exceeding `PETENT_MAX_COUNT_IN_MOMENT`.
4. Handles MQ requests from petents:
   - `LoaderMessagesEnum::NowyPetent`: if capacity is available, increments `clientsInBuilding` and replies to the petent with `PetentMessagesEnum::WejdzDoBudynku`.
   - `LoaderMessagesEnum::PetentOpuszczaBudynek`: decrements `clientsInBuilding`.

## MQ usage conventions

- Petent requests entry by sending a `Message` to the petent queue with:
  - `mtype = loader PID`
  - `group = MessageGroup::Loader`
  - `messageType.loaderType = LoaderMessagesEnum::NowyPetent`
  - `senderId = PID`
- Loader replies to the petent queue with:
  - `mtype = requester PID`
  - `group = MessageGroup::Petent`
  - `messageType.petentType = PetentMessagesEnum::WejdzDoBudynku`
  - `receiverId = requester PID`

Other message types follow the same `group` + `messageType` pattern.

## Ticket queue length
Ticket queue length is defined as the number of petents in the building who sent a request to the ticket machine and have not yet received the ticket. Petent notifies biletomat with:

- `BiletomatMessagesEnum::PetentCzekaNaBilet`
- `BiletomatMessagesEnum::PetentOdebralBilet`

Loader updates `ticketQueueLen` in shared memory based on these messages and controls the number of active ticket machines.