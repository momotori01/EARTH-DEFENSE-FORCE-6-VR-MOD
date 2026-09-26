#pragma once
#include "camera_math.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace edf6vr {
// The exact current EDF resolve CALL return addresses. Shadow and unknown
// callers are intentionally excluded; these are RVAs, not absolute pointers.
enum class NativeWorldViewKind { Other, Main, Far };
constexpr NativeWorldViewKind ClassifyNativeWorldResolve(std::uintptr_t returnRva) noexcept {
    return returnRva==0x11B6B22 ? NativeWorldViewKind::Main :
           returnRva==0x11B680E ? NativeWorldViewKind::Far : NativeWorldViewKind::Other;
}

// Original Umbra setter input is a row-vector camera-to-world matrix.
// EDF11E3C33..11E3D70 applies Ry(pi) AFTER its inverse: screen right is
// NEGATIVE row0, not row0. Translation is row3; up/forward are rows1/2.
constexpr float NativeWorldCameraOffset(unsigned eye,float ipd) noexcept {
    return (eye?-.5f:.5f)*ipd;
}
bool ValidNativeWorldCamera(const Matrix&) noexcept;

// Pure fixed-capacity state, independently testable with an explicit clock.
// The public functions below provide synchronization and the real clock.
class NativeWorldViewCache {
public:
    static constexpr std::size_t kCapacity=16;
    static constexpr std::uint64_t kMaxAgeMs=250;
    void Reset() noexcept;
    void Record(void* camera,const Matrix&,std::uint64_t nowMs) noexcept;
    bool Prepare(void* camera,std::uint64_t frame,unsigned eye,float ipd,
                 std::uint64_t nowMs,Matrix& original,Matrix& shifted) noexcept;
private:
    struct Camera {
        void* camera=nullptr;
        Matrix latest{},baseline{};
        std::uint64_t latestAt=0,pairAt=0,frame=0,touchedAt=0;
        float ipd=0;
        bool latestValid=false,pairValid=false;
    };
    std::array<Camera,kCapacity> cameras{};
};

// Call Record ONLY for the engine's original setter argument. When submitting
// an eye or restoring the matrix, call the original engine setter directly;
// feeding a shifted matrix back into Record would violate this API's contract.
void RecordNativeWorldCamera(void* camera,const Matrix&) noexcept;
// Eye0 latches the original left-eye baseline; eye1 subtracts IPD along row0
// (EDF's converted screen-right axis). Both eyes use that immutable baseline, even if logic publishes a newer
// original between them. Frame tokens must be nonzero and monotonic per camera.
// Failure leaves both outputs untouched. Do not alias the two output matrices.
bool PrepareNativeWorldEye(void* camera,std::uint64_t frame,unsigned eye,float ipd,
                           Matrix& original,Matrix& shifted) noexcept;
void ResetNativeWorldCameras() noexcept;
}
