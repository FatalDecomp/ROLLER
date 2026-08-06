#ifndef ROLLER_FSM_H
#define ROLLER_FSM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*FsmEnterFn)(void *context);
typedef void (*FsmUpdateFn)(void *context);
typedef void (*FsmDrawFn)(void *context);
typedef void (*FsmExitFn)(void *context);

typedef struct {
  FsmEnterFn enter;
  FsmUpdateFn update;
  FsmDrawFn draw;
  FsmExitFn exit;
} FsmState;

typedef enum {
  FSM_OK = 0,
  FSM_ERROR_INVALID_ARGUMENT,
  FSM_ERROR_INVALID_STATE,
  FSM_ERROR_STACK_FULL,
  FSM_ERROR_STACK_EMPTY
} FsmError;

typedef struct {
  int transitioned;
  int from_state;
  int to_state;
  FsmError error;
} FsmStepResult;

typedef struct {
  const FsmState *states;
  FsmState uniform_state;
  int state_count;
  int current_state;
  int pending_state;
  void *context;
} FsmMachine;

FsmError fsm_init(FsmMachine *machine,
                  const FsmState *states,
                  int state_count,
                  int initial_state,
                  void *context);
FsmError fsm_init_uniform(FsmMachine *machine,
                          const FsmState *state,
                          int state_count,
                          int initial_state,
                          void *context);
FsmError fsm_request(FsmMachine *machine, int target_state);
FsmStepResult fsm_transition(FsmMachine *machine, int target_state);
FsmError fsm_enter_current_state(FsmMachine *machine);
FsmError fsm_exit_current_state(FsmMachine *machine);
FsmStepResult fsm_step(FsmMachine *machine);
int fsm_current(const FsmMachine *machine);
int fsm_pending(const FsmMachine *machine);
int fsm_state_is_valid(const FsmMachine *machine, int state);

#ifdef __cplusplus
}
#endif

#endif
