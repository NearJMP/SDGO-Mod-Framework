#include "sdgo/friend_unit_gift_client_transport.h"
#include <windows.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace fixture {

using namespace sdgo::friend_unit_gift;
using namespace sdgo::friend_unit_gift::client;

std::array<std::uint8_t, 0x100> session{};
std::vector<std::vector<std::uint8_t>> sent;
std::vector<TransportEvent> events;
std::vector<Reply> event_replies;
int send_result = 1;

std::uint8_t* __cdecl Session() {
    return session.data();
}

int __cdecl CaptureSend(
    void* native_session,
    int connection,
    const std::uint8_t* packet,
    int size,
    int flags) {
    assert(native_session == session.data());
    assert(connection == 7);
    assert(flags == 0);
    assert(packet);
    assert(size == static_cast<int>(kNativeProbeBytes) ||
           size == static_cast<int>(kCarrierBytes));
    sent.emplace_back(packet, packet + size);
    return send_result;
}

#if defined(_M_IX86)
int __fastcall NativeSendShim(
    void* native_session,
    void*,
    int connection,
    const std::uint8_t* packet,
    int size,
    int flags) {
    return CaptureSend(
        native_session, connection, packet, size, flags);
}
#else
int NativeSendShim(
    void* native_session,
    int connection,
    const std::uint8_t* packet,
    int size,
    int flags) {
    return CaptureSend(
        native_session, connection, packet, size, flags);
}
#endif

void __cdecl Sink(
    const TransportEvent* event,
    void*) {
    assert(event);
    events.push_back(*event);
    events.back().reply = nullptr;
    if (event->reply) {
        event_replies.push_back(*event->reply);
    }
}

void SetConnection(int value) {
    std::memcpy(session.data() + 0x14, &value, sizeof(value));
}

Request DecodeSentRequest(std::size_t index) {
    assert(index < sent.size());
    assert(sent[index].size() == kCarrierBytes);
    auto bytes = sent[index];
    wire::Write32(bytes.data() + 2, 4001);
    assert(MarkAgentValidated(bytes.data(), bytes.size()));
    Request request;
    assert(DecodeRequest(bytes.data(), bytes.size(), &request));
    return request;
}

std::array<std::uint8_t, kCarrierBytes> ReplyBytes(
    const Reply& reply) {
    std::array<std::uint8_t, kCarrierBytes> bytes{};
    assert(EncodeReply(4001, reply, &bytes));
    return bytes;
}

void CompleteDiscovery(Transport& transport,std::uint32_t capabilities) {
    Reply hello;
    hello.command=Command::hello;
    hello.capabilities=capabilities;
    hello.server_epoch=500;
    auto bytes=ReplyBytes(hello);
    assert(transport.OnStoragePacket(kNativeReplyOpcode,bytes.data(),static_cast<std::uint32_t>(bytes.size())));
    assert(transport.State()==ConnectionState::probing_expanded);
    const auto request=DecodeSentRequest(sent.size()-1);
    assert(request.command==Command::hello && (request.flags&envelope_read_only));
    hello.request_id=request.request_id;
    hello.flags=envelope_expanded_probe;
    bytes=ReplyBytes(hello);
    assert(transport.OnStoragePacket(kNativeReplyOpcode,bytes.data(),static_cast<std::uint32_t>(bytes.size())));
    assert(transport.Ready());
}

} // namespace fixture

int main() {
    using namespace fixture;

    SetConnection(7);
    Transport transport;
    assert(transport.BindNative(
        &Session,
        reinterpret_cast<Transport::NativeSend>(&NativeSendShim)));
    transport.SetSink(&Sink, nullptr);
    assert(transport.State() == ConnectionState::disconnected);

    assert(transport.BeginProbeAt(100));
    assert(sent.size() == 1);
    assert(sent[0].size() == kNativeProbeBytes);
    assert(IsProbeBeforeAuthentication(
        sent[0].data(), sent[0].size()));
    assert(transport.State() == ConnectionState::probing_native);
    assert(transport.NativeProbeSendAttempts() == 1);
    assert(transport.NativeProbeSendSuccesses() == 1);
    assert(transport.NativeProbeLastSendResult() == 1);
    assert(transport.NativeProbeLastPacketSize() ==
           kNativeProbeBytes);
    assert(transport.NativeProbeLastSession() != 0);
    assert(transport.NativeProbeReplyCount() == 0);
    assert(transport.NativeProbeLastReason() ==
           NativeProbeReason::awaiting_reply);

    std::array<std::uint8_t, kCarrierBytes> native_reply{};
    wire::Write16(native_reply.data(), kNativeReplyOpcode);
    assert(!transport.OnStoragePacket(
        kNativeReplyOpcode,
        native_reply.data(),
        static_cast<std::uint32_t>(native_reply.size())));
    assert(transport.NativeProbeLastReason() ==
           NativeProbeReason::stock_not_extension);

    constexpr std::uint32_t capabilities =
        capability_protocol |
        capability_expanded_requests |
        capability_offer_enabled |
        capability_resolve_existing |
        capability_provenance |
        capability_agent_validated;
    Reply hello;
    hello.command = Command::hello;
    hello.capabilities = capabilities;
    hello.server_epoch = 77;
    auto bytes = ReplyBytes(hello);
    assert(transport.OnStoragePacket(
        kNativeReplyOpcode,
        bytes.data(),
        static_cast<std::uint32_t>(bytes.size())));
    assert(transport.NativeProbeReplyCount() == 1);
    assert(transport.NativeProbeLastReason() ==
           NativeProbeReason::capability_accepted);
    assert(sent.size() == 2);
    assert(transport.State() ==
           ConnectionState::probing_expanded);
    const auto expanded = DecodeSentRequest(1);
    assert(expanded.command == Command::hello);
    assert(!Empty(expanded.request_id));
    assert(expanded.flags & envelope_expanded_probe);
    // A real first-use policy installation exceeded the three-second native
    // discovery window. The confirmed extension must still accept its reply.
    transport.TickAt(GetTickCount64() + 5000);
    assert(transport.State() == ConnectionState::probing_expanded);

    hello.request_id = expanded.request_id;
    hello.flags = envelope_expanded_probe;
    bytes = ReplyBytes(hello);
    assert(transport.OnStoragePacket(
        kNativeReplyOpcode,
        bytes.data(),
        static_cast<std::uint32_t>(bytes.size())));
    assert(transport.State() == ConnectionState::ready);
    assert(transport.Capabilities() == capabilities);
    assert(transport.ServerEpoch() == 77);

    Request list;
    list.command = Command::list_inbox;
    list.page = 1;
    assert(transport.SendAt(list, 1000));
    assert(sent.size() == 3);
    const auto sent_list = DecodeSentRequest(2);
    Reply list_reply;
    list_reply.command = Command::list_inbox;
    list_reply.request_id = sent_list.request_id;
    list_reply.capabilities = capabilities;
    list_reply.total_count = 0;
    list_reply.server_epoch = 78;
    bytes = ReplyBytes(list_reply);
    assert(transport.OnStoragePacket(
        kNativeReplyOpcode,
        bytes.data(),
        static_cast<std::uint32_t>(bytes.size())));
    assert(!transport.HasOutstanding());
    assert(transport.ServerEpoch() == 78);

    // Other Mods share this carrier through extension commands. Extension
    // replies advertise only the carrier baseline (0x23), not the complete
    // friend-gift policy. They must deliver their payload without revoking the
    // gift capabilities established by Hello.
    const auto extension_sent_base = sent.size();
    const auto extension_reply_base = event_replies.size();
    Request extension;
    extension.command = static_cast<Command>(kExtensionCommandFirst);
    extension.extension_bytes = 2;
    extension.extension[0] = 0xAB;
    extension.extension[1] = 0xCD;
    assert(transport.SendAt(extension, 1500));
    assert(sent.size() == extension_sent_base + 1);
    const auto sent_extension = DecodeSentRequest(extension_sent_base);
    Reply extension_reply;
    extension_reply.command = extension.command;
    extension_reply.request_id = sent_extension.request_id;
    extension_reply.status = WireStatus::success;
    extension_reply.capabilities =
        capability_protocol |
        capability_expanded_requests |
        capability_agent_validated;
    extension_reply.server_epoch = 79;
    extension_reply.extension_bytes = 2;
    extension_reply.extension[0] = 0x12;
    extension_reply.extension[1] = 0x34;
    bytes = ReplyBytes(extension_reply);
    assert(transport.OnStoragePacket(
        kNativeReplyOpcode,
        bytes.data(),
        static_cast<std::uint32_t>(bytes.size())));
    assert(transport.State() == ConnectionState::ready);
    assert(transport.Capabilities() == capabilities);
    assert(transport.ServerEpoch() == 78);
    assert(event_replies.size() == extension_reply_base + 1);
    assert(event_replies.back().command == extension.command);
    assert(event_replies.back().extension_bytes == 2);
    assert(event_replies.back().extension[0] == 0x12);
    assert(event_replies.back().extension[1] == 0x34);

    // Ordinary gift replies remain authoritative. A policy response that
    // removes offer support must still revoke that capability.
    Request capability_refresh;
    capability_refresh.command = Command::list_inbox;
    capability_refresh.page = 1;
    assert(transport.SendAt(capability_refresh, 1600));
    const auto sent_refresh = DecodeSentRequest(extension_sent_base + 1);
    Reply capability_reply;
    capability_reply.command = Command::list_inbox;
    capability_reply.request_id = sent_refresh.request_id;
    capability_reply.status = WireStatus::success;
    capability_reply.capabilities =
        capabilities & ~capability_offer_enabled;
    capability_reply.server_epoch = 80;
    capability_reply.page = 1;
    bytes = ReplyBytes(capability_reply);
    assert(transport.OnStoragePacket(
        kNativeReplyOpcode,
        bytes.data(),
        static_cast<std::uint32_t>(bytes.size())));
    assert(transport.Capabilities() == capability_reply.capabilities);
    assert(transport.ServerEpoch() == 80);

    Request disabled_preview;
    disabled_preview.command = Command::preview_offer;
    disabled_preview.receiver_id = 4002;
    disabled_preview.unit_uid = 0x1122334455667788ull;
    assert(!transport.SendAt(disabled_preview, 1700));

    // Restore the advertised policy for the mutation/receipt cases below.
    Request capability_restore;
    capability_restore.command = Command::list_inbox;
    capability_restore.page = 1;
    assert(transport.SendAt(capability_restore, 1800));
    const auto sent_restore = DecodeSentRequest(extension_sent_base + 2);
    capability_reply.request_id = sent_restore.request_id;
    capability_reply.capabilities = capabilities;
    capability_reply.server_epoch = 81;
    bytes = ReplyBytes(capability_reply);
    assert(transport.OnStoragePacket(
        kNativeReplyOpcode,
        bytes.data(),
        static_cast<std::uint32_t>(bytes.size())));
    assert(transport.Capabilities() == capabilities);

    Request offer;
    offer.command = Command::offer;
    offer.receiver_id = 4002;
    offer.unit_uid = 0x1122334455667788ull;
    assert(transport.SendAt(offer, 2000));
    assert(sent.size() == extension_sent_base + 4);
    transport.TickAt(12000);
    assert(transport.State() == ConnectionState::uncertain);
    assert(transport.MutationUncertain());
    assert(sent.size() == extension_sent_base + 4);
    assert(!transport.SendAt(offer, 12001));

    const auto sent_offer = DecodeSentRequest(extension_sent_base + 3);
    assert(transport.UncertainRequestId() ==
           sent_offer.request_id);
    assert(transport.UncertainCommand() == Command::offer);
    Request receipt;
    receipt.command = Command::receipt;
    assert(transport.SendAt(receipt, 12002));
    assert(sent.size() == extension_sent_base + 5);
    const auto sent_receipt = DecodeSentRequest(extension_sent_base + 4);
    assert(sent_receipt.request_id == sent_offer.request_id);
    assert(sent_receipt.gift_id == 0);
    Reply receipt_reply;
    receipt_reply.command = Command::receipt;
    receipt_reply.request_id = sent_receipt.request_id;
    receipt_reply.capabilities = capabilities;
    receipt_reply.status = WireStatus::outcome_unknown;
    receipt_reply.unit_uid = sent_offer.unit_uid;
    receipt_reply.server_epoch = 79;
    bytes = ReplyBytes(receipt_reply);
    assert(transport.OnStoragePacket(
        kNativeReplyOpcode,
        bytes.data(),
        static_cast<std::uint32_t>(bytes.size())));
    assert(transport.State() == ConnectionState::uncertain);
    assert(transport.MutationUncertain());
    assert(transport.UncertainRequestId() ==
           sent_offer.request_id);

    // A receipt query reuses the original mutation GUID. It never creates a
    // second mutation identity while the committed outcome is unresolved.
    assert(transport.SendAt(receipt, 12003));
    assert(sent.size() == extension_sent_base + 6);
    const auto repeated_receipt = DecodeSentRequest(extension_sent_base + 5);
    assert(repeated_receipt.request_id == sent_offer.request_id);
    receipt_reply.request_id = repeated_receipt.request_id;
    receipt_reply.status = WireStatus::success;
    receipt_reply.gift_id = 9001;
    receipt_reply.server_epoch = 80;
    bytes = ReplyBytes(receipt_reply);
    assert(transport.OnStoragePacket(
        kNativeReplyOpcode,
        bytes.data(),
        static_cast<std::uint32_t>(bytes.size())));
    assert(transport.State() == ConnectionState::ready);
    assert(!transport.MutationUncertain());
    assert(Empty(transport.UncertainRequestId()));

    // The server can also acknowledge that execution was issued while its
    // post-commit result/receipt read remains unknown. Treat that explicit
    // reply exactly like an acknowledgement lost in transit.
    Request second_offer = offer;
    second_offer.unit_uid = 0x8877665544332211ull;
    assert(transport.SendAt(second_offer, 12500));
    assert(sent.size() == extension_sent_base + 7);
    const auto sent_second_offer = DecodeSentRequest(extension_sent_base + 6);
    Reply uncertain_reply;
    uncertain_reply.command = Command::offer;
    uncertain_reply.status = WireStatus::outcome_unknown;
    uncertain_reply.request_id = sent_second_offer.request_id;
    uncertain_reply.capabilities = capabilities;
    uncertain_reply.unit_uid = sent_second_offer.unit_uid;
    uncertain_reply.server_epoch = 81;
    bytes = ReplyBytes(uncertain_reply);
    assert(transport.OnStoragePacket(
        kNativeReplyOpcode,
        bytes.data(),
        static_cast<std::uint32_t>(bytes.size())));
    assert(transport.State() == ConnectionState::uncertain);
    assert(transport.MutationUncertain());
    assert(transport.UncertainRequestId() ==
           sent_second_offer.request_id);

    Request second_receipt;
    second_receipt.command = Command::receipt;
    assert(transport.SendAt(second_receipt, 12501));
    assert(sent.size() == extension_sent_base + 8);
    const auto sent_second_receipt = DecodeSentRequest(extension_sent_base + 7);
    assert(sent_second_receipt.request_id ==
           sent_second_offer.request_id);
    receipt_reply.request_id = sent_second_receipt.request_id;
    receipt_reply.status = WireStatus::success;
    receipt_reply.gift_id = 9002;
    receipt_reply.unit_uid = sent_second_offer.unit_uid;
    receipt_reply.server_epoch = 82;
    bytes = ReplyBytes(receipt_reply);
    assert(transport.OnStoragePacket(
        kNativeReplyOpcode,
        bytes.data(),
        static_cast<std::uint32_t>(bytes.size())));
    assert(transport.State() == ConnectionState::ready);
    assert(!transport.MutationUncertain());

    Request preview;
    preview.command = Command::preview_offer;
    preview.receiver_id = 4002;
    preview.unit_uid = offer.unit_uid;
    auto without_offer = hello;
    without_offer.request_id = {};
    without_offer.flags = 0;
    without_offer.capabilities =
        capabilities & ~capability_offer_enabled;

    // A session change clears capability and requires a fresh safe probe.
    SetConnection(8);
    transport.TickAt(13000);
    assert(transport.State() == ConnectionState::idle);
    assert(!transport.SendAt(preview, 13001));

    bool saw_timeout = false;
    for (const auto& event : events) {
        if (event.kind == EventKind::request_failed &&
            event.status == WireStatus::timed_out &&
            event.command == Command::offer)
            saw_timeout = true;
    }
    assert(saw_timeout);

    SetConnection(7);
    Transport vanilla;
    assert(vanilla.BindNative(
        &Session,
        reinterpret_cast<Transport::NativeSend>(&NativeSendShim)));
    assert(vanilla.BeginProbeAt(20000));
    assert(vanilla.NativeProbeSendSuccesses() == 1);
    vanilla.TickAt(23000);
    assert(vanilla.State() == ConnectionState::unavailable);
    assert(vanilla.NativeProbeReplyCount() == 0);
    assert(vanilla.NativeProbeLastReason() ==
           NativeProbeReason::reply_timeout);

    Transport unresponsive_extension;
    assert(unresponsive_extension.BindNative(
        &Session, reinterpret_cast<Transport::NativeSend>(&NativeSendShim)));
    assert(unresponsive_extension.BeginProbeAt(30000));
    Reply extension_hello;
    extension_hello.command = Command::hello;
    extension_hello.capabilities = capabilities;
    extension_hello.server_epoch = 90;
    bytes = ReplyBytes(extension_hello);
    assert(unresponsive_extension.OnStoragePacket(
        kNativeReplyOpcode, bytes.data(), static_cast<std::uint32_t>(bytes.size())));
    assert(unresponsive_extension.State() == ConnectionState::probing_expanded);
    unresponsive_extension.TickAt(GetTickCount64() + 180000);
    assert(unresponsive_extension.State() == ConnectionState::unavailable);
    assert(!unresponsive_extension.Ready());

    // A dropped first reply must not hide the feature until another login.
    Transport recovery;
    assert(recovery.BindNative(&Session,reinterpret_cast<Transport::NativeSend>(&NativeSendShim)));
    const auto recovery_base=sent.size();
    assert(recovery.BeginProbeAt(200000));
    recovery.TickAt(203000);
    assert(recovery.State()==ConnectionState::unavailable);
    recovery.TickAt(207999);
    assert(recovery.State()==ConnectionState::unavailable);
    assert(!recovery.BeginProbeAt(207999));
    recovery.TickAt(208000);
    assert(recovery.State()==ConnectionState::idle);
    assert(sent.size()==recovery_base+1); // Tick only permits the authenticated caller to retry.
    assert(recovery.BeginProbeAt(208000));
    assert(recovery.NativeProbeSendAttempts()==2);
    CompleteDiscovery(recovery,capabilities);
    assert(recovery.Capabilities()==capabilities);
    assert(sent.size()==recovery_base+3);
    const auto recovered_count=sent.size();
    recovery.TickAt(GetTickCount64()+1000000);
    assert(recovery.Ready()&&sent.size()==recovered_count);

    Transport backoff;
    assert(backoff.BindNative(&Session,reinterpret_cast<Transport::NativeSend>(&NativeSendShim)));
    std::uint64_t at=500000;
    for(const auto delay:{5000u,15000u,60000u,60000u}) {
        assert(backoff.BeginProbeAt(at));
        backoff.TickAt(at+3000);
        assert(backoff.State()==ConnectionState::unavailable);
        backoff.TickAt(at+3000+delay-1);
        assert(backoff.State()==ConnectionState::unavailable);
        backoff.TickAt(at+3000+delay);
        assert(backoff.State()==ConnectionState::idle);
        at+=3000+delay;
    }
    // New connections receive a fresh discovery budget without a previous delay.
    SetConnection(-1);backoff.TickAt(at+1);
    SetConnection(7);backoff.TickAt(at+2);
    assert(backoff.State()==ConnectionState::idle);
    assert(backoff.BeginProbeAt(at+2));
    backoff.TickAt(at+3002);
    backoff.TickAt(at+8002);
    assert(backoff.State()==ConnectionState::idle);

    // A stock inbox reply can belong to another native request. It does not
    // authorize expansion, nor prove the missing capability reply unsupported.
    Transport stock;
    assert(stock.BindNative(&Session,reinterpret_cast<Transport::NativeSend>(&NativeSendShim)));
    assert(stock.BeginProbeAt(10000));
    assert(!stock.OnStoragePacket(kNativeReplyOpcode,native_reply.data(),static_cast<std::uint32_t>(native_reply.size())));
    stock.TickAt(13000);stock.TickAt(17999);
    assert(stock.State()==ConnectionState::unavailable&&stock.NativeProbeSendAttempts()==1);
    stock.TickAt(18000);
    assert(stock.State()==ConnectionState::idle&&stock.BeginProbeAt(18000));
    assert(sent.back().size()==kNativeProbeBytes&&stock.NativeProbeSendAttempts()==2);
    // An authenticated protocol rejection is authoritative and stops retries.
    Transport rejected;
    assert(rejected.BindNative(&Session,reinterpret_cast<Transport::NativeSend>(&NativeSendShim)));
    assert(rejected.BeginProbeAt(10000));
    Reply refusal;refusal.command=Command::hello;bytes=ReplyBytes(refusal);
    assert(rejected.OnStoragePacket(kNativeReplyOpcode,bytes.data(),static_cast<std::uint32_t>(bytes.size())));
    rejected.TickAt(1000000);
    assert(rejected.State()==ConnectionState::unavailable&&rejected.NativeProbeSendAttempts()==1);
    Transport disabled_policy;
    assert(disabled_policy.BindNative(&Session,reinterpret_cast<Transport::NativeSend>(&NativeSendShim)));
    assert(disabled_policy.BeginProbeAt(10000));
    CompleteDiscovery(disabled_policy,capabilities&~capability_offer_enabled);
    const auto disabled_count=sent.size();
    disabled_policy.TickAt(GetTickCount64()+1000000);
    assert(disabled_policy.Ready()&&!(disabled_policy.Capabilities()&capability_offer_enabled));
    assert(sent.size()==disabled_count&&!disabled_policy.SendAt(offer,1000000));

    Transport failed_send;
    assert(failed_send.BindNative(&Session,reinterpret_cast<Transport::NativeSend>(&NativeSendShim)));
    send_result=0;
    assert(!failed_send.BeginProbeAt(10000));
    assert(failed_send.State()==ConnectionState::unavailable);
    assert(!failed_send.BeginProbeAt(14999));
    assert(failed_send.NativeProbeSendAttempts()==1);
    send_result=1;failed_send.TickAt(15000);
    assert(failed_send.BeginProbeAt(15000));
    assert(failed_send.NativeProbeSendAttempts()==2);

    Transport late;
    assert(late.BindNative(&Session,reinterpret_cast<Transport::NativeSend>(&NativeSendShim)));
    assert(late.BeginProbeAt(10000));
    Reply discovery_hello;
    discovery_hello.command=Command::hello;
    discovery_hello.capabilities=capabilities;
    discovery_hello.server_epoch=501;
    bytes=ReplyBytes(discovery_hello);
    assert(late.OnStoragePacket(kNativeReplyOpcode,bytes.data(),static_cast<std::uint32_t>(bytes.size())));
    const auto delayed_request=DecodeSentRequest(sent.size()-1);
    const auto expired_at=GetTickCount64()+140000;
    late.TickAt(expired_at);late.TickAt(expired_at+5000);
    assert(late.State()==ConnectionState::idle);
    assert(late.BeginProbeAt(expired_at+5000));
    auto delayed=discovery_hello;delayed.request_id=delayed_request.request_id;delayed.flags=envelope_expanded_probe;
    bytes=ReplyBytes(delayed);
    assert(late.OnStoragePacket(kNativeReplyOpcode,bytes.data(),static_cast<std::uint32_t>(bytes.size())));
    assert(late.State()==ConnectionState::probing_native);
    CompleteDiscovery(late,capabilities);

    Transport uncertain;
    assert(uncertain.BindNative(&Session,reinterpret_cast<Transport::NativeSend>(&NativeSendShim)));
    assert(uncertain.BeginProbeAt(10000));CompleteDiscovery(uncertain,capabilities);
    const auto mutation_at=GetTickCount64();
    assert(uncertain.SendAt(offer,mutation_at));
    uncertain.TickAt(mutation_at+10000);
    assert(uncertain.MutationUncertain());
    const auto mutation_count=sent.size();
    uncertain.TickAt(mutation_at+1000000);
    assert(uncertain.State()==ConnectionState::uncertain&&sent.size()==mutation_count);

    std::cout << "friend unit gift transport tests passed\n";
    return 0;
}
