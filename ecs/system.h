// System execution contracts.
//
// Main-thread systems mutate World directly. Worker systems never receive a
// mutable World: they capture immutable input on the main thread, execute an
// independent task on a worker, and submit World commands for the fixed-tick
// barrier. This keeps EnTT structural mutation on the main thread.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace snt::ecs {

class World;
class WorkerCommandContext;

enum class SystemThreadAffinity : uint8_t {
    MainThread,
    Worker,
};

enum class SystemResourceAccessMode : uint8_t {
    Read,
    Write,
};

// A named World or runtime resource accessed during a fixed tick. Names are
// intentionally stable strings so diagnostics and future editor tooling can
// describe a dependency graph without exposing component implementation types.
struct SystemResourceAccess {
    std::string resource;
    SystemResourceAccessMode mode = SystemResourceAccessMode::Read;
};

// Every system registered with SystemScheduler declares its thread affinity
// and resource access. A worker descriptor describes data read while capture()
// runs and data written later through WorkerCommandContext.
struct SystemMetadata {
    std::string name;
    SystemThreadAffinity affinity = SystemThreadAffinity::MainThread;
    std::vector<SystemResourceAccess> resources;
};

// Base class for all ECS systems.
class System {
public:
    virtual ~System() = default;

    // Systems outside SystemScheduler may keep the empty default metadata.
    // Registration requires a non-empty name and a MainThread affinity.
    virtual SystemMetadata metadata() const { return {}; }

    // Called once per frame. `dt` is delta time in seconds.
    virtual void update(World& world, float dt) = 0;
};

// Immutable unit of worker work created by IWorkerSystem::capture(). It has
// no World parameter, so it can only request mutations through commands that
// the scheduler applies after every worker task reaches the tick barrier.
// WorkerCommandContext also offers a synchronous pure-compute parallel_for;
// child jobs must fill independent result slots and leave command enqueueing
// to this parent task after the child handle completes.
class IWorkerTask {
public:
    virtual ~IWorkerTask() = default;
    virtual void execute(WorkerCommandContext& commands) = 0;
};

// Worker systems prepare a self-contained task while the main thread owns the
// World. capture() must copy every value the task needs; retaining registry
// pointers or references is forbidden by this contract.
class IWorkerSystem {
public:
    virtual ~IWorkerSystem() = default;
    virtual SystemMetadata metadata() const = 0;
    virtual std::unique_ptr<IWorkerTask> capture(const World& world,
                                                  float dt) = 0;
};

}  // namespace snt::ecs
