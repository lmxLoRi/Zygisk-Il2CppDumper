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
        for method_id in function["ManagedMethodIds"]:
            assert method_id in method_by_id, f"missing managed method: {method_id}"
            assert method_by_id[method_id]["NativeFunctionId"] == function["Id"]

    assert managed["NativeBindingCount"] == bound_count
    print(f"valid: {len(methods)} managed methods, {len(functions)} native functions, "
          f"{bound_count} bindings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
