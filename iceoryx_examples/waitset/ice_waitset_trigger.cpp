// Copyright (c) 2020 by Apex.AI Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "iceoryx_posh/popo/wait_set.hpp"
#include "iceoryx_posh/runtime/posh_runtime.hpp"
#include "iceoryx_utils/cxx/optional.hpp"

#include <iostream>
#include <thread>

// The two events the MyTriggerClass offers
enum class MyTriggerClassEvents
{
    PERFORMED_ACTION,
    ACTIVATE
};

// Triggerable class which has two events an both events can be
// attached to a WaitSet.
class MyTriggerClass
{
  public:
    // When you call this method you will trigger the ACTIVATE event
    void activate(const int activationCode) noexcept
    {
        m_activationCode = activationCode;
        m_isActivated = true;
        m_activateTrigger.trigger();
    }

    // Calling this method will trigger the PERFORMED_ACTION event
    void performAction() noexcept
    {
        m_hasPerformedAction = true;
        m_actionTrigger.trigger();
    }

    int getActivationCode() const noexcept
    {
        return m_activationCode;
    }

    // required by the m_actionTrigger to ask the class if it was triggered
    bool hasPerformedAction() const noexcept
    {
        return m_hasPerformedAction;
    }

    // required by the m_activateTrigger to ask the class if it was triggered
    bool isActivated() const noexcept
    {
        return m_isActivated;
    }

    // reset PERFORMED_ACTION and ACTIVATE event
    void reset() noexcept
    {
        m_hasPerformedAction = false;
        m_isActivated = false;
    }

    // This method attaches an event of the class to a waitset.
    // The event is choosen by the event parameter. Additionally, you can
    // set a triggerId to group multiple instances and a custom callback.
    iox::cxx::expected<iox::popo::WaitSetError>
    attachToWaitset(iox::popo::WaitSet& waitset,
                    const MyTriggerClassEvents event,
                    const uint64_t triggerId,
                    const iox::popo::Trigger::Callback<MyTriggerClass> callback) noexcept
    {
        switch (event)
        {
        case MyTriggerClassEvents::PERFORMED_ACTION:
        {
            return waitset
                .acquireTrigger(this,
                                // trigger calls this method to ask if it was triggered
                                {this, &MyTriggerClass::hasPerformedAction},
                                // method which will be called when the waitset goes out of scope
                                {this, &MyTriggerClass::unsetTrigger},
                                triggerId,
                                callback)
                // assigning the acquired trigger from the waitset to m_actionTrigger
                .and_then([this](iox::popo::Trigger& trigger) { m_actionTrigger = std::move(trigger); });
        }
        case MyTriggerClassEvents::ACTIVATE:
        {
            return waitset
                .acquireTrigger(this,
                                // trigger calls this method to ask if it was triggered
                                {this, &MyTriggerClass::isActivated},
                                // method which will be called when the waitset goes out of scope
                                {this, &MyTriggerClass::unsetTrigger},
                                triggerId,
                                callback)
                // assigning the acquired trigger from the waitset to m_activateTrigger
                .and_then([this](iox::popo::Trigger& trigger) { m_activateTrigger = std::move(trigger); });
        }
        }

        return iox::cxx::success<>();
    }

    // we offer the waitset a method to invalidate trigger if it goes
    // out of scope
    void unsetTrigger(const iox::popo::Trigger& trigger)
    {
        if (trigger.isLogicalEqualTo(m_actionTrigger))
        {
            m_actionTrigger.reset();
        }
        else if (trigger.isLogicalEqualTo(m_activateTrigger))
        {
            m_activateTrigger.reset();
        }
    }

    static void callOnAction(MyTriggerClass* const triggerClassPtr)
    {
        std::cout << "action performed" << std::endl;
    }

  private:
    int m_activationCode = 0;
    bool m_hasPerformedAction = false;
    bool m_isActivated = false;

    iox::popo::Trigger m_actionTrigger;
    iox::popo::Trigger m_activateTrigger;
};

iox::cxx::optional<iox::popo::WaitSet> waitset;
iox::cxx::optional<MyTriggerClass> triggerClass;

constexpr uint64_t ACTIVATE_ID = 0;
constexpr uint64_t ACTION_ID = 1;

void callOnActivate(MyTriggerClass* const triggerClassPtr)
{
    std::cout << "activated with code: " << triggerClassPtr->getActivationCode() << std::endl;
}

void backgroundThread()
{
    while (true)
    {
        auto triggerStateVector = waitset->wait();
        for (auto& triggerState : triggerStateVector)
        {
            if (triggerState.getTriggerId() == ACTIVATE_ID)
            {
                triggerState();
                triggerState.getOrigin<MyTriggerClass>()->reset();
            }
            else if (triggerState.getTriggerId() == ACTION_ID)
            {
                triggerState();
                triggerState.getOrigin<MyTriggerClass>()->reset();
            }
        }
    }
}

int main()
{
    iox::runtime::PoshRuntime::getInstance("/iox-ex-waitset-trigger");

    waitset.emplace();
    triggerClass.emplace();

    triggerClass->attachToWaitset(*waitset, MyTriggerClassEvents::ACTIVATE, ACTIVATE_ID, callOnActivate);
    triggerClass->attachToWaitset(
        *waitset, MyTriggerClassEvents::PERFORMED_ACTION, ACTION_ID, MyTriggerClass::callOnAction);

    std::thread t(backgroundThread);
    std::thread triggerThread([&] {
        int activationCode = 1;
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            triggerClass->activate(activationCode++);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            triggerClass->performAction();
        }
    });

    triggerThread.join();
    t.join();
    return (EXIT_SUCCESS);
}
