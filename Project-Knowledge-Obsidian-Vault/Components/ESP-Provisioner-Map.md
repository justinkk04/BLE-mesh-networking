# ESP Provisioner Map

File: `ESP/ESP-Provisioner/main/main.c`

## Role

Auto-discovers and configures mesh devices.

## Key Sections

1. Composition parsing  
   `parse_composition_data()` at `:191`
2. Model bind chain  
   `bind_next_model()` at `:337`
3. Group subscription  
   `subscribe_vendor_model_to_group()` at `:312`
4. Provisioning callbacks  
   `provisioning_cb()` at `:524`
5. Config response handling  
   `config_client_cb()` at `:647`

## What to Read First

1. `bind_next_model()`  
2. `config_client_cb()`  
3. `provisioning_cb()`

That gives the setup lifecycle end-to-end.

## Output You Should Expect

A fully configured node has:

- app key added
- required models bound
- vendor server subscribed to `0xC000` (if supported)
