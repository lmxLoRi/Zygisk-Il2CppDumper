#!/usr/bin/env python3
import argparse
import json
from pathlib import Path


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate managed.json/native.json links")
    parser.add_argument("directory", type=Path, help="directory containing both JSON files")
    parser.add_argument("--managed", default="managed.json", help="managed JSON file name")
    parser.add_argument("--native", default="native.json", help="native JSON file name")
    args = parser.parse_args()

    managed_path = args.directory / args.managed
    native_path = args.directory / args.native
    if args.managed == "managed.json" and not managed_path.exists():
        managed_path = args.directory / "zygisk-managed.json"
    if args.native == "native.json" and not native_path.exists():
        native_path = args.directory / "zygisk-native.json"

    managed = load_json(managed_path)
    native = load_json(native_path)

    assert managed["SchemaVersion"] == 2
    assert native["SchemaVersion"] == 2

    methods = managed["ManagedMethods"]
    functions = native["NativeFunctions"]
    method_by_id = {item["Id"]: item for item in methods}
    function_by_id = {item["Id"]: item for item in functions}
    assert len(method_by_id) == len(methods), "duplicate managed method ID"
    assert len(function_by_id) == len(functions), "duplicate native function ID"
    assert managed["MethodCount"] == len(methods)
    assert native["NativeFunctionCount"] == len(functions)

    image_base = native["Image"]["ObservedImageBase"]
    bound_count = 0
    for method in methods:
        function_id = method["NativeFunctionId"]
        assert method["HasObservedMethodPointer"] or function_id is None
        if method["HasNativeImplementation"]:
            assert method["HasObservedMethodPointer"]
            assert method["BindingKind"] == "runtime_method_pointer"
        if method["BindingKind"] == "runtime_stub_candidate":
            assert not method["HasNativeImplementation"]
        if function_id is None:
            assert method["BindingKind"] is None
            continue
        bound_count += 1
        assert function_id in function_by_id, f"missing native function: {function_id}"
        assert method["Id"] in function_by_id[function_id]["ManagedMethodIds"]

    for function in functions:
        assert function["ObservedVA"] == image_base + function["RVA"]
        assert function["IsShared"] == (len(function["ManagedMethodIds"]) > 1)
        sources = function.get("Sources") or ([function["Source"]] if function.get("Source") else [])
        assert sources, f"native function has no source: {function['Id']}"
        for method_id in function["ManagedMethodIds"]:
            assert method_id in method_by_id, f"missing managed method: {method_id}"
            assert method_by_id[method_id]["NativeFunctionId"] == function["Id"]

    assert managed["NativeBindingCount"] == bound_count
    registration_only = sum(not item["ManagedMethodIds"] for item in functions)
    registration = native.get("Registration", {})
    generic_instances = native.get("GenericInstances", [])
    for instance in generic_instances:
        native_id = instance["NativeFunctionId"]
        if native_id is not None:
            assert native_id in function_by_id
        managed_id = instance.get("ManagedMethodId")
        if managed_id is not None:
            assert managed_id in method_by_id
            assert method_by_id[managed_id].get("MethodDefinitionIndex") == instance["MethodDefinitionIndex"]
    metadata_slots = native.get("MetadataSlots", [])
    slot_keys = {(item["SlotRVA"], item["TargetKind"], item["TargetId"]) for item in metadata_slots}
    assert len(slot_keys) == len(metadata_slots), "duplicate metadata slot relation"
    for slot in metadata_slots:
        assert slot["SlotRVA"] >= 0
        if slot["TargetKind"] == "method_info":
            assert slot["TargetId"] in method_by_id
    if registration.get("MetadataSlotCount") is not None:
        assert registration["MetadataSlotCount"] == len(metadata_slots)
    pointer_references = native.get("PointerReferences", [])
    reference_keys = {(item["ReferenceRVA"], item["TargetKind"], item["TargetId"])
                      for item in pointer_references}
    assert len(reference_keys) == len(pointer_references), "duplicate pointer reference"
    if registration.get("PointerReferenceCount") is not None:
        assert registration["PointerReferenceCount"] == len(pointer_references)
    print(f"valid: {len(methods)} managed methods, {len(functions)} native functions, "
          f"{bound_count} bindings, {registration_only} registration-only functions, "
          f"{len(generic_instances)} generic instances, {len(metadata_slots)} metadata slots, "
          f"{len(pointer_references)} pointer references")
    if registration:
        print(f"registration: {registration.get('Status', 'unknown')}, "
              f"{registration.get('GenericMethodPointerCount', 0)} generic pointers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
