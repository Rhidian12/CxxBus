#include <benchmark/benchmark.h>
#include <sys/types.h>

#include <cstdint>
#include <ranges>

#include "src/DBus.h"
#include "src/DBusMessage.h"
#include "src/DBusTypes.h"
#include "src/IncomingDBusMessage.h"

using namespace cxxbus;

static void BM_Message(benchmark::State& state)
{
  DBusMessage message =
      DBusMessage::Method("Foo").Path(ObjectPath{"/com/dbus/CxxTest"}).Destination("com.dbus.cxxtest");
  auto rawMessage = message.Serialize(0);

  for (auto _ : state)
  {
    auto headerData =
        UnmarshalDBusType<MultipleCompleteTypes<uint8_t, uint8_t, uint8_t, uint8_t, uint32_t, uint32_t, uint32_t>>(
            std::ranges::to<std::vector>(rawMessage | std::views::take(FIRST_HEADER_PART_SIZE)), "yyyyuuu");
    uint32_t const messageLength = headerData.GetType<4>();
    uint32_t const headerFieldArrLength = headerData.GetType<6>();
    uint32_t const serial = headerData.GetType<5>();
    DBusMessageType const messageType = static_cast<DBusMessageType>(headerData.GetType<1>());

    uint32_t remainingSizeToRead{FIRST_HEADER_PART_SIZE + headerFieldArrLength};
    uint32_t nrOfPaddingBytes = AddPaddingToSize(remainingSizeToRead, DBUS_MESSAGE_BODY_ALIGNMENT);

    IncomingDBusMessage incomingMessage{
        DBusMessageHeader{std::span<byte const>{rawMessage.begin(),
                                                rawMessage.begin() + FIRST_HEADER_PART_SIZE + headerFieldArrLength},
                          serial, messageType, headerFieldArrLength, messageLength},
        std::ranges::to<std::vector>(
            rawMessage | std::views::drop(FIRST_HEADER_PART_SIZE + headerFieldArrLength + nrOfPaddingBytes))};
  }
}

BENCHMARK(BM_Message);
