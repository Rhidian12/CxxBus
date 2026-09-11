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
    DBusMessageHeader messageHeader{
        std::ranges::to<std::vector>(rawMessage | std::views::take(FIRST_HEADER_PART_SIZE))};

    messageHeader.ParseHeaderFieldLength(std::ranges::to<std::vector>(
        rawMessage | std::views::drop(FIRST_HEADER_PART_SIZE) | std::views::take(sizeof(uint32_t))));

    uint32_t arrPointer{FIRST_HEADER_PART_SIZE};
    messageHeader.ParseRemainderOfHeader(
        std::ranges::to<std::vector>(rawMessage | std::views::take(FIRST_HEADER_PART_SIZE + sizeof(uint32_t) +
                                                                   messageHeader.GetHeaderFieldsLength())),
        arrPointer);

    uint32_t const oldArrPointer{arrPointer};
    AddPaddingToSize(arrPointer, DBUS_MESSAGE_BODY_ALIGNMENT);
    uint32_t const nrOfPaddingBytes{arrPointer - oldArrPointer};

    // Skip over the padding, we don't care about it
    IncomingDBusMessage incomingMessage{
        std::move(messageHeader),
        std::ranges::to<std::vector>(rawMessage |
                                     std::views::drop(messageHeader.GetHeaderFieldsLength() + nrOfPaddingBytes))};
  }
}

BENCHMARK(BM_Message);
