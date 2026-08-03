# Output Schema v2

## Stable identities

- Managed method: `managed:<assembly>:<token>` when a metadata token is available.
- Native function: `native:rva:<hex-rva>`.
- Runtime VA is observational and changes with ASLR. RVA is the stable native address.

An RVA is not a managed method identity. Generic sharing and runtime stubs can bind many managed methods to one native function.

## managed.json

`ManagedMethods` contains every method returned by the runtime class API. `HasObservedMethodPointer` preserves the raw runtime observation, while `HasNativeImplementation` excludes abstract/interface stub pointers. `BindingKind` marks those ambiguous pointers as `runtime_stub_candidate` instead of presenting them as implementations.

Each parameter is structured as `Position`, `Name`, `Type`, and `Modifier`. `ManagedSignature` is intended for display and search; consumers should use the structured fields for matching.

## native.json

`NativeFunctions` contains one record per observed RVA. `ManagedMethodIds` is the reverse edge to `managed.json`; `IsShared` indicates more than one managed method currently points to the function.

`Registration` reports the independently validated runtime registration anchors, codegen modules, and generic method pointer count. Registration-only native functions intentionally have an empty `ManagedMethodIds` array until method-spec decoding can provide a reliable managed identity.

`GenericInstances` links each decoded method spec to its native function, base managed method, class type arguments, and method type arguments. `MethodDefinitionIndex` is also emitted on managed methods so this relation remains machine-checkable without parsing display signatures.

`MetadataSlots` contains initialized slots that point directly to objects independently observed through exported IL2CPP APIs. `PointerReferences` contains additional exact references discovered from registration type tables. Both are runtime observations and are intentionally distinguished from the unsupported complete static metadata usage table.

`Capabilities` is authoritative. Empty arrays whose capability is `false` mean that the current dumper did not collect that category, not that the target contains no such records.

## Unknown data

Missing or unsupported values use JSON `null`, an empty array, or a disabled capability. The dumper does not synthesize native symbols or metadata records without runtime evidence.
