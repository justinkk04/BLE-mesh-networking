# Lab 2 - Trace threshold and balancing

Goal: Understand how automatic power balancing works.

## Commands

1. `threshold 5000`
2. Wait 2-3 PM cycles
3. `priority 1`
4. Wait 2-3 cycles
5. `priority off`
6. `threshold off`

## What to Observe

1. PM starts polling with `ALL:READ`
2. Total power compared to budget
3. Duty nudges sent to nodes
4. Priority changes node share behavior
5. On `threshold off`, duties restore toward target

## Code Checkpoints

- Enable threshold: `gateway-pi5/gateway.py:173`
- Poll all: `gateway-pi5/gateway.py:412`
- Evaluate and adjust: `gateway-pi5/gateway.py:452`
- Priority balancing: `gateway-pi5/gateway.py:639`
- Set priority: `gateway-pi5/gateway.py:218`
- Clear priority: `gateway-pi5/gateway.py:235`

## Completion Criteria

You can explain:

1. What budget is
2. Why PM uses `ALL:READ`
3. Difference between equal-share and priority-share balancing
