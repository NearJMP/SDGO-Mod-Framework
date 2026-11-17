#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "sdgo/game_test.h"
#include "sdgo/pvp_ai_client_observer.h"
#include "../battle_ai/native_capabilities.h"
#include "../match_ai/forced_pad_observer.h"
#include "../game_test_shared/runner.h"

// Opt-in passive witness (LaunchPadWitness=1) for imported launch pads in a PvP GameTest.
//
// When the authenticated local unit touches a forced-move volume (scene kind 0x21) the
// client's own collision update copies the volume's speed (+0x1F4), update count (+0x1F8)
// and forward axis (+0x1FC) into the unit's movement controller, enters its Force state
// (controller node +0x124) and sends the ordinary request to the Battle host. This witness
// only reads that state, the copied payload and the node position: it records the start
// of each flight, the position where the Force state ends and the native travel model's
// prediction. Locally owned AI participants use match_ai's request/notice bridge; their
// passive request and server-notice counters are reported alongside.
class LaunchPadWitness {
    static constexpr std::size_t kManagerLocalUnit = 0x88;
    static constexpr std::size_t kUnitPilot = 0x2B68;
    static constexpr std::size_t kUnitHp = 0x38;
    static constexpr std::size_t kUnitNode = 0x08;
    static constexpr std::size_t kNodePosition = 0x88;
    static constexpr std::size_t kUnitMovement = 0x9C8;
    static constexpr std::size_t kForceState = 0x124;
    static constexpr std::size_t kForceSpeed = 0x1F4;
    static constexpr std::size_t kForceUpdates = 0x1F8;
    static constexpr std::size_t kForceDirection = 0x1FC;
    static constexpr int kMaxReportedFlights = 16;

    struct Flight {
        battle_ai::Vec3 start{}, last{}, direction{};
        float speed = 0, updates = 0;
        uint64_t startedAt = 0;
    };

    void** manager_ = nullptr;
    SdgoPvpAiClientGetForcedPadStatusV1Fn getPads_ = nullptr;
    uint32_t sequence_ = 0;
    bool flying_ = false;
    Flight flight_{};
    uint32_t started_ = 0, completed_ = 0, accurate_ = 0;
    double worstError_ = 0;
    SdgoPvpAiClientForcedPadStatusV1 pads_{};
    bool padsKnown_ = false;

    static bool Enabled(const SdgoGameTestContextV1* context) {
        return context->api->SettingInt(sdgo_test::mod, "LaunchPadWitness", 0) == 1;
    }
    bool LocalUnit(const SdgoGameTestContextV1* context, uint32_t pilot, uint8_t*& unit) {
        using namespace battle_ai::native::detail;
        if (!manager_)
            manager_ = reinterpret_cast<void**>(context->api->Symbol(sdgo_test::mod, "game.unit_manager"));
        uint8_t* manager = nullptr;
        uint32_t actual = 0;
        int32_t hp = 0;
        return manager_ && ReadPointer(reinterpret_cast<uint8_t*>(manager_), 0, manager) &&
               ReadPointer(manager, kManagerLocalUnit, unit) && unit &&
               Read(unit, kUnitPilot, actual) && actual == pilot && Read(unit, kUnitHp, hp) && hp > 0;
    }
    static bool Position(uint8_t* unit, battle_ai::Vec3& position) {
        using namespace battle_ai::native::detail;
        uint8_t* node = nullptr;
        return ReadPointer(unit, kUnitNode, node) && Read(node, kNodePosition, position) &&
               battle_ai::Finite(position);
    }
    static double Travel(float speed, float updates) {
        // Native ForceMove: direction * speed per logic update, damped 0.9 by the movement
        // surface, plus the terminal update's second damping (owner/maps/compiler.py).
        return double(speed) * (std::ceil(updates) * 0.9 + 0.81);
    }
    void Report(const SdgoGameTestContextV1* context, const battle_ai::Vec3& end, uint64_t now) {
        ++completed_;
        const double travel = Travel(flight_.speed, flight_.updates);
        const battle_ai::Vec3 predicted = flight_.start + flight_.direction * static_cast<float>(travel);
        const battle_ai::Vec3 moved = end - flight_.start;
        const double along = battle_ai::Dot(moved, flight_.direction);
        const double error = battle_ai::Length(end - predicted);
        // Trigger-edge overlap and the first observed frame offset the start by at most one
        // native step plus the trigger half-size; collision may shorten the flight.
        const double tolerance = (std::max)(150.0, travel * 0.12) + flight_.speed;
        if (along > travel * 0.5 && error <= tolerance) ++accurate_;
        if (error > worstError_) worstError_ = error;
        if (completed_ <= kMaxReportedFlights) {
            char text[400];
            sprintf_s(text,
                "native launch pad flight n=%u start=(%.1f,%.1f,%.1f) end=(%.1f,%.1f,%.1f) predicted=(%.1f,%.1f,%.1f) "
                "speed=%.4f updates=%.1f direction=(%.5f,%.5f,%.5f) along=%.1f/%.1f error=%.1f tolerance=%.1f ms=%llu",
                completed_, flight_.start.x, flight_.start.y, flight_.start.z, end.x, end.y, end.z,
                predicted.x, predicted.y, predicted.z, flight_.speed, flight_.updates,
                flight_.direction.x, flight_.direction.y, flight_.direction.z, along, travel, error, tolerance,
                static_cast<unsigned long long>(now - flight_.startedAt));
            context->Observe(text);
        }
    }

public:
    void Observe(const SdgoGameTestContextV1* context, const SdgoPvpAiClientSnapshotV1& live) {
        if (!Enabled(context)) return;
        using namespace battle_ai::native::detail;
        if (sequence_ != live.received_battle_start_count) {
            sequence_ = live.received_battle_start_count;
            flying_ = false;
            padsKnown_ = false;
        }
        if (!getPads_)
            getPads_ = reinterpret_cast<SdgoPvpAiClientGetForcedPadStatusV1Fn>(
                GetProcAddress(GetModuleHandleW(L"match_ai.dll"), SDGO_PVP_AI_CLIENT_FORCED_PAD_STATUS_EXPORT));
        if (getPads_) {
            SdgoPvpAiClientForcedPadStatusV1 pads{};
            pads.size = sizeof(pads);
            pads.version = SDGO_PVP_AI_CLIENT_FORCED_PAD_STATUS_VERSION;
            if (getPads_(&pads) && pads.battle_start_sequence == sequence_) {
                pads_ = pads;
                padsKnown_ = true;
            }
        }
        uint8_t* unit = nullptr;
        uint8_t* movement = nullptr;
        battle_ai::Vec3 position{};
        const uint64_t now = GetTickCount64();
        if (!LocalUnit(context, live.profile_pilot_id, unit) || !ReadPointer(unit, kUnitMovement, movement) ||
            !Position(unit, position)) {
            flying_ = false;          // death or a missing unit ends an unfinished flight unreported
            return;
        }
        const bool forced = InState(movement, kForceState);
        if (forced && !flying_) {
            Flight next{};
            next.start = next.last = position;
            next.startedAt = now;
            if (Read(movement, kForceSpeed, next.speed) && Read(movement, kForceUpdates, next.updates) &&
                Read(movement, kForceDirection, next.direction) && std::isfinite(next.speed) &&
                std::isfinite(next.updates) && battle_ai::Finite(next.direction) && next.speed > 0 &&
                next.updates > 0) {
                flight_ = next;
                flying_ = true;
                ++started_;
            }
        } else if (forced && flying_) {
            flight_.last = position;
        } else if (!forced && flying_) {
            flying_ = false;
            Report(context, position, now);
        }
    }

    void Finish(const SdgoGameTestContextV1* context) {
        if (!Enabled(context)) return;
        uint32_t requests = 0, notices = 0, displacements = 0, failures = 0, timeouts = 0;
        if (padsKnown_) {
            for (uint32_t i = 0; i < pads_.bot_count && i < 12; ++i) {
                requests += pads_.bots[i].request_count;
                notices += pads_.bots[i].notice_count;
                displacements += pads_.bots[i].displacement_count;
                failures += pads_.bots[i].send_failure_count;
                timeouts += pads_.bots[i].timeout_count;
            }
        }
        char text[320];
        sprintf_s(text,
            "native launch pad summary local_started=%u local_completed=%u local_accurate=%u worst_error=%.1f "
            "ai_witness=%u ai_requests=%u ai_server_notices=%u ai_displacements=%u ai_send_failures=%u ai_timeouts=%u",
            started_, completed_, accurate_, worstError_, padsKnown_ ? 1u : 0u, requests, notices, displacements,
            failures, timeouts);
        context->Observe(text);
        context->Assert(completed_ > 0,
            "the local pilot completed at least one native forced-move flight from an imported launch pad");
        context->Assert(completed_ > 0 && accurate_ == completed_,
            "every observed local launch-pad flight travelled along its volume and ended near the native travel model");
        context->Assert(padsKnown_ && notices > 0 && failures == 0,
            "AI participants' launch-pad requests were relayed by the Battle host as native notices");
    }
};
