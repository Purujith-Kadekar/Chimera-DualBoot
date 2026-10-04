#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  state_coordinator.h — F10: State Machine Coordination Layer
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>

#define SC_MAX_STATE_MACHINES 8
#define SC_LOG_SIZE 16

struct StateMachineDesc {
  const char* name;
  bool (*isValidForTransition)(void* ctx);
  void* ctx;
  bool registered;
};

struct TransitionLogEntry {
  unsigned long timestamp;
  const char* fromState;
  const char* toState;
  bool success;
  const char* failureReason;
};

class StateMachineCoordinator {
public:
  StateMachineCoordinator() = default;

  bool registerStateMachine(const char* name,
                            bool (*isValidFn)(void*),
                            void* ctx = nullptr);

  bool beginTransition(const char* fromState, const char* toState);
  void completeTransition(const char* fromState, const char* toState);
  void rollback();

  void printLog() const;
  int getRegisteredCount() const;
  bool isTransitionInProgress() const { return _transitionInProgress; }

private:
  StateMachineDesc _machines[SC_MAX_STATE_MACHINES];
  int _machineCount = 0;

  bool _snapshot[SC_MAX_STATE_MACHINES];
  int _snapshotCount = 0;

  TransitionLogEntry _log[SC_LOG_SIZE];
  int _logHead = 0;
  int _logCount = 0;

  bool _transitionInProgress = false;
  const char* _pendingFrom = nullptr;
  const char* _pendingTo = nullptr;

  void _addLogEntry(const char* from, const char* to, bool success,
                    const char* reason = nullptr);
};

extern StateMachineCoordinator stateCoord;
