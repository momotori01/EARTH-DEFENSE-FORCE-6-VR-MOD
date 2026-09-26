#pragma once
#include "image_profile.h"
#include <cstring>

namespace edf6vr {
// Whether the Ranger is in his dash, asked of the game rather than guessed from
// speed or animation.
//
// AssultSoldier::State_Dash is EDF.dll+0x552040. Like every state in this
// engine it keeps its working data in a ut::sgs::Any, and the object inside
// that Any is an Actual<Work> whose RTTI name carries the state function that
// built it:
//
//   .?AU?$Actual@UWork@?1??State_Dash@AssultSoldier@@IEAAX...
//
// So the state machine says which state it is in, in plain text. The Any lives
// at *(soldier+0x1EF8)+0x30 -- the state entry loads exactly that pointer,
// destroys what the Any held and writes the new vtable into +0x30 before doing
// anything else.
//
// Why this matters for VR: the dash entry writes (0,0,0,1) to soldier+0x1200
// and its update reads the move input at soldier+0xD50. The run therefore
// leaves along the body's facing at the instant the state is entered, and this
// mod had made that facing the controller's, so the dash went wherever the
// weapon happened to be pointing rather than where the player was looking.
//
// Nothing here writes: the state is read, and the caller decides what to send
// as the soldier's aim for those frames.
inline constexpr std::size_t kDashStateContext=0x1EF8;
inline constexpr std::size_t kDashStateAny=0x30;
inline constexpr char kDashStateName[]="State_Dash@AssultSoldier";

// The name of the state a soldier is in, or null. Its storage belongs to the
// image, so the pointer keeps as long as the game is loaded.
inline const char* SoldierStateName(const ImageProfile& image,const void* soldier) noexcept {
    if(!image.base || !Readable(soldier,kDashStateContext+8)) return nullptr;
    __try {
        const auto context=*reinterpret_cast<void* const*>(
            static_cast<const unsigned char*>(soldier)+kDashStateContext);
        if(!Readable(context,kDashStateAny+8)) return nullptr;
        return TypeName(image,static_cast<const unsigned char*>(context)+kDashStateAny);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
inline bool RangerDashing(const ImageProfile& image,const void* soldier) noexcept {
    const auto name=SoldierStateName(image,soldier);
    return name && std::strstr(name,kDashStateName)!=nullptr;
}
}
