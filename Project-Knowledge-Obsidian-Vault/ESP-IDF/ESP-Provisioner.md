# ESP Provisioner (ESP-IDF)

Main file:

- `ESP/ESP-Provisioner/main/main.c`

Read these functions first:

1. `provisioning_cb()` at `:524`
2. `config_client_cb()` at `:647`
3. `bind_next_model()` at `:337`
4. `subscribe_vendor_model_to_group()` at `:312`
5. `parse_composition_data()` at `:191`

Important concept:

- `msg_role = ROLE_PROVISIONER` at `:275`, `:301`, `:326`

Then read [[Components/ESP-Provisioner-Map]].
