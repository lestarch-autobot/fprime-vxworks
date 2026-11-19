#include <sysLib.h>
#include <taskLib.h>  // need it for VX_FP_TASK
#include <vxWorks.h>

#include <unistd.h>
#include <cerrno>
#include <climits>
#include <cstring>

#include "Fw/Types/Assert.hpp"
#include "Fw/Types/BasicTypes.hpp"
#include "Os/Task.hpp"

#include "VxWorks/Os/Task.hpp"

namespace Os {
namespace VxWorks {
namespace Task {

//!< This static function implements the
//!< FUNCPTR signature required by VxWorks7 and calls the routine
//!< with the provides arg. This wrapper function is expected to be
//!< passed in to taskCreate.
static int myRoutineWrapper(Os::TaskInterface::taskRoutine routine,  //!< Routine that should run in a thread
                            void* arg                                //!< Argument passed to the routine
) {
    routine(arg);
    return OK;
}

VxWorksTask::~VxWorksTask() {
    this->deregister(this);
}

void VxWorksTask::onStart() {}

Os::TaskInterface::Status VxWorksTask::start(const Arguments& arguments) {
    FW_ASSERT(arguments.m_routine != nullptr);
    const Arguments wrapped_arguments = this->prepareStart(this, arguments);
    const Status status = this->spawn(wrapped_arguments);
    this->completeStart(this, status, wrapped_arguments);
    return status;
}

Os::TaskInterface::Status VxWorksTask::spawn(const Arguments& arguments) {

    // Get taskName into a non-const variable because that is what
    // VxWorks' taskCreate wants.
    char taskName[Os::TaskString::BUFFER_SIZE(Os::TaskString::STRING_SIZE)];
    auto minimumStringSize = FW_MIN(sizeof(taskName), arguments.m_name.getCapacity());
    memcpy(taskName, arguments.m_name.toChar(), minimumStringSize);

    if (arguments.m_stackSize == Os::TaskInterface::TASK_DEFAULT) {
        return Os::TaskInterface::Status::INVALID_STACK;
    }

    if (arguments.m_priority == Os::TaskInterface::TASK_PRIORITY_DEFAULT) {
        return Os::TaskInterface::Status::INVALID_PRIORITY;
    }

    this->m_handle.m_task_descriptor = taskCreate(
        taskName, static_cast<int>(arguments.m_priority), VX_FP_TASK, static_cast<size_t>(arguments.m_stackSize),
        reinterpret_cast<FUNCPTR>(myRoutineWrapper), reinterpret_cast<_Vx_usr_arg_t>(arguments.m_routine),
        reinterpret_cast<_Vx_usr_arg_t>(arguments.m_routine_argument), 0, 0, 0, 0, 0, 0, 0, 0);

    if (this->m_handle.m_task_descriptor == TASK_ID_NULL) {
        return Os::TaskInterface::Status::UNKNOWN_ERROR;
    }

#ifdef _WRS_CONFIG_SMP
    // when cpuAffinity is set to TASK_DEFAULT, the affinity will be assigned by the scheduler
    if (arguments.m_cpuAffinity != Os::TaskInterface::TASK_DEFAULT) {
        cpuset_t aff;

        CPUSET_ZERO(aff);
        CPUSET_SET(aff, arguments.m_cpuAffinity);

        if (taskCpuAffinitySet(this->m_handle.m_task_descriptor, aff) == ERROR) {
            /* Either CPUs are not enabled or we are in UP mode */
            (void)taskDelete(this->m_handle.m_task_descriptor);
            return Os::TaskInterface::Status::INVALID_AFFINITY;
        }
    }
#endif

    // Activate task
    if (taskActivate(this->m_handle.m_task_descriptor) == ERROR) {
        return Os::TaskInterface::Status::UNKNOWN_ERROR;
    }

    return Os::TaskInterface::Status::OP_OK;
}

Os::TaskInterface::Status VxWorksTask::join() {
    if (not this->prepareJoin(this)) {
        return Os::TaskInterface::Status::INVALID_STATE;
    }
    // Joining is not supported on VxWorks
    const Status status = Os::TaskInterface::Status::JOIN_ERROR;
    this->completeJoin(this, status);
    return status;
}

TaskHandle* VxWorksTask::getHandle() {
    return &this->m_handle;
}

void VxWorksTask::suspend(Os::TaskInterface::SuspensionType suspensionType) {
    STATUS status = taskSuspend(this->m_handle.m_task_descriptor);
    FW_ASSERT(status == VXWORKS_OK, static_cast<FwAssertArgType>(status));
    this->completeSuspend(this, suspensionType);
}

void VxWorksTask::resume() {
    STATUS status = taskResume(this->m_handle.m_task_descriptor);
    FW_ASSERT(status == OK, static_cast<FwAssertArgType>(status));
}

Os::TaskInterface::Status VxWorksTask::_delay(const Fw::TimeInterval& interval) {
    // Lookup the number of clock ticks for the specified
    // number of milliseconds. Use the ticks to call the
    // task delay function call.
    Os::TaskInterface::Status status = Os::TaskInterface::DELAY_ERROR;

    U32 milliseconds = interval.getUSeconds() / 1000 + interval.getSeconds() * 1000;
    U32 ticks = ((milliseconds * sysClkRateGet() + 999) / 1000);

    if (taskDelay(ticks) == OK) {
        status = Os::TaskInterface::OP_OK;
    }

    return status;
}

}  // end namespace Task
}  // end namespace VxWorks
}  // end namespace Os
