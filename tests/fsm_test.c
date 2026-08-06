#include "fsm.h"
#include "fsm_stack.h"

#include <assert.h>
#include <string.h>

#define TEST_STATE_COUNT 4
#define LOG_CAPACITY 64

typedef struct {
  char log[LOG_CAPACITY];
  int log_len;
  FsmMachine *machine;
  FsmStack *stack;
  int request_from_update;
  int request_target;
} TestContext;

static void append_log(TestContext *context, char entry)
{
  assert(context->log_len < LOG_CAPACITY - 1);
  context->log[context->log_len++] = entry;
  context->log[context->log_len] = '\0';
}

static void enter_a(void *context) { append_log((TestContext *)context, 'A'); }
static void update_a(void *context) { append_log((TestContext *)context, 'a'); }
static void draw_a(void *context) { append_log((TestContext *)context, '1'); }
static void exit_a(void *context) { append_log((TestContext *)context, 'x'); }

static void enter_b(void *context) { append_log((TestContext *)context, 'B'); }
static void draw_b(void *context) { append_log((TestContext *)context, '2'); }
static void exit_b(void *context) { append_log((TestContext *)context, 'y'); }

static void update_b(void *context)
{
  TestContext *test_context = (TestContext *)context;
  append_log(test_context, 'b');
  if (test_context->request_from_update)
    assert(fsm_request(test_context->machine, test_context->request_target) == FSM_OK);

  if (test_context->stack)
    assert(fsm_stack_pop(test_context->stack) == FSM_OK);
}

static const FsmState test_states[TEST_STATE_COUNT] = {
  { enter_a, update_a, draw_a, exit_a },
  { enter_b, update_b, draw_b, exit_b },
  { NULL, NULL, NULL, NULL },
  { NULL, NULL, NULL, NULL },
};

static TestContext new_context(void)
{
  TestContext context;
  memset(&context, 0, sizeof(context));
  context.request_target = -1;
  return context;
}

static void test_init_enters_initial_state(void)
{
  FsmMachine machine;
  TestContext context = new_context();

  assert(fsm_init(&machine, test_states, TEST_STATE_COUNT, 0, &context) == FSM_OK);

  assert(fsm_current(&machine) == 0);
  assert(fsm_pending(&machine) == 0);
  assert(strcmp(context.log, "A") == 0);
}

static void test_request_is_deferred_until_step(void)
{
  FsmMachine machine;
  TestContext context = new_context();

  assert(fsm_init(&machine, test_states, TEST_STATE_COUNT, 0, &context) == FSM_OK);
  assert(fsm_request(&machine, 1) == FSM_OK);

  assert(fsm_current(&machine) == 0);
  assert(fsm_pending(&machine) == 1);
  assert(strcmp(context.log, "A") == 0);
}

static void test_uniform_state_profile_uses_same_callbacks_for_all_states(void)
{
  FsmMachine machine;
  FsmStepResult result;
  TestContext context = new_context();
  FsmState uniform_state = { enter_a, update_a, draw_a, exit_a };

  assert(fsm_init_uniform(&machine, &uniform_state, TEST_STATE_COUNT, 0, &context) == FSM_OK);
  assert(fsm_request(&machine, 2) == FSM_OK);

  result = fsm_step(&machine);

  assert(result.error == FSM_OK);
  assert(result.transitioned);
  assert(result.from_state == 0);
  assert(result.to_state == 2);
  assert(fsm_current(&machine) == 2);
  assert(fsm_pending(&machine) == 2);
  assert(strcmp(context.log, "AxAa1") == 0);
}


static void test_transition_applies_immediately_without_update_or_draw(void)
{
  FsmMachine machine;
  FsmStepResult result;
  TestContext context = new_context();

  assert(fsm_init(&machine, test_states, TEST_STATE_COUNT, 0, &context) == FSM_OK);

  result = fsm_transition(&machine, 1);

  assert(result.error == FSM_OK);
  assert(result.transitioned);
  assert(result.from_state == 0);
  assert(result.to_state == 1);
  assert(fsm_current(&machine) == 1);
  assert(fsm_pending(&machine) == 1);
  assert(strcmp(context.log, "AxB") == 0);
}

static void test_step_applies_transition_before_update_and_draw(void)
{
  FsmMachine machine;
  FsmStepResult result;
  TestContext context = new_context();

  assert(fsm_init(&machine, test_states, TEST_STATE_COUNT, 0, &context) == FSM_OK);
  assert(fsm_request(&machine, 1) == FSM_OK);

  result = fsm_step(&machine);

  assert(result.error == FSM_OK);
  assert(result.transitioned);
  assert(result.from_state == 0);
  assert(result.to_state == 1);
  assert(fsm_current(&machine) == 1);
  assert(fsm_pending(&machine) == 1);
  assert(strcmp(context.log, "AxBb2") == 0);
}

static void test_step_skips_draw_when_update_requests_transition(void)
{
  FsmMachine machine;
  FsmStepResult result;
  TestContext context = new_context();

  assert(fsm_init(&machine, test_states, TEST_STATE_COUNT, 1, &context) == FSM_OK);
  context.machine = &machine;
  context.request_from_update = 1;
  context.request_target = 0;

  result = fsm_step(&machine);

  assert(result.error == FSM_OK);
  assert(!result.transitioned);
  assert(fsm_current(&machine) == 1);
  assert(fsm_pending(&machine) == 0);
  assert(strcmp(context.log, "Bb") == 0);
}

static void test_step_draws_restored_state_after_overlay_pop(void)
{
  FsmMachine machine;
  FsmStack stack;
  int storage[1];
  FsmStepResult result;
  TestContext context = new_context();

  assert(fsm_init(&machine, test_states, TEST_STATE_COUNT, 0, &context) == FSM_OK);
  assert(fsm_stack_init(&stack, &machine, storage, 1) == FSM_OK);
  assert(fsm_stack_push(&stack, 1) == FSM_OK);
  context.stack = &stack;

  result = fsm_step(&machine);

  assert(result.error == FSM_OK);
  assert(!result.transitioned);
  assert(fsm_current(&machine) == 0);
  assert(fsm_pending(&machine) == 0);
  assert(strcmp(context.log, "ABby1") == 0);
}

static void test_invalid_request_returns_error_and_keeps_state(void)
{
  FsmMachine machine;
  TestContext context = new_context();

  assert(fsm_init(&machine, test_states, TEST_STATE_COUNT, 0, &context) == FSM_OK);
  assert(fsm_request(&machine, TEST_STATE_COUNT) == FSM_ERROR_INVALID_STATE);

  assert(fsm_current(&machine) == 0);
  assert(fsm_pending(&machine) == 0);
  assert(strcmp(context.log, "A") == 0);
}

static void test_stack_push_enters_overlay_without_exiting_suspended_state(void)
{
  FsmMachine machine;
  FsmStack stack;
  int storage[1];
  TestContext context = new_context();

  assert(fsm_init(&machine, test_states, TEST_STATE_COUNT, 0, &context) == FSM_OK);
  assert(fsm_stack_init(&stack, &machine, storage, 1) == FSM_OK);

  assert(fsm_stack_push(&stack, 1) == FSM_OK);

  assert(fsm_current(&machine) == 1);
  assert(fsm_pending(&machine) == 1);
  assert(fsm_stack_count(&stack) == 1);
  assert(strcmp(context.log, "AB") == 0);
}

static void test_stack_push_works_with_uniform_state_profile(void)
{
  FsmMachine machine;
  FsmStack stack;
  int storage[1];
  TestContext context = new_context();
  FsmState uniform_state = { enter_a, update_a, draw_a, exit_a };

  assert(fsm_init_uniform(&machine, &uniform_state, TEST_STATE_COUNT, 0, &context) == FSM_OK);
  assert(fsm_stack_init(&stack, &machine, storage, 1) == FSM_OK);

  assert(fsm_stack_push(&stack, 2) == FSM_OK);

  assert(fsm_current(&machine) == 2);
  assert(fsm_pending(&machine) == 2);
  assert(fsm_stack_count(&stack) == 1);
  assert(strcmp(context.log, "AA") == 0);
}

static void test_stack_pop_exits_overlay_without_reentering_suspended_state(void)
{
  FsmMachine machine;
  FsmStack stack;
  int storage[1];
  TestContext context = new_context();

  assert(fsm_init(&machine, test_states, TEST_STATE_COUNT, 0, &context) == FSM_OK);
  assert(fsm_stack_init(&stack, &machine, storage, 1) == FSM_OK);
  assert(fsm_stack_push(&stack, 1) == FSM_OK);

  assert(fsm_stack_pop(&stack) == FSM_OK);

  assert(fsm_current(&machine) == 0);
  assert(fsm_pending(&machine) == 0);
  assert(fsm_stack_count(&stack) == 0);
  assert(strcmp(context.log, "ABy") == 0);
}

static void test_stack_overflow_and_empty_pop_return_errors(void)
{
  FsmMachine machine;
  FsmStack stack;
  int storage[1];
  TestContext context = new_context();

  assert(fsm_init(&machine, test_states, TEST_STATE_COUNT, 0, &context) == FSM_OK);
  assert(fsm_stack_init(&stack, &machine, storage, 1) == FSM_OK);

  assert(fsm_stack_pop(&stack) == FSM_ERROR_STACK_EMPTY);
  assert(fsm_stack_push(&stack, 1) == FSM_OK);
  assert(fsm_stack_push(&stack, 2) == FSM_ERROR_STACK_FULL);
  assert(fsm_current(&machine) == 1);
  assert(fsm_stack_count(&stack) == 1);
}

int main(void)
{
  test_init_enters_initial_state();
  test_request_is_deferred_until_step();
  test_uniform_state_profile_uses_same_callbacks_for_all_states();

  test_transition_applies_immediately_without_update_or_draw();
  test_step_applies_transition_before_update_and_draw();
  test_step_skips_draw_when_update_requests_transition();
  test_step_draws_restored_state_after_overlay_pop();
  test_invalid_request_returns_error_and_keeps_state();
  test_stack_push_enters_overlay_without_exiting_suspended_state();
  test_stack_push_works_with_uniform_state_profile();
  test_stack_pop_exits_overlay_without_reentering_suspended_state();
  test_stack_overflow_and_empty_pop_return_errors();
  return 0;
}
