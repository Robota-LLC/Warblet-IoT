// Run from a checkout of the Warblet core: go run <this file> <decoder.star>.
package main

import (
    "fmt"
    "os"
    "go.starlark.net/starlark"
    starjson "go.starlark.net/lib/json"
    "go.starlark.net/starlarkstruct"
)

func main() {
    thread := &starlark.Thread{Name: "camera-decoder-check"}
    // Core's sandbox accepts bytes in json.decode; the upstream module wants
    // string. This adapter duplicates only that conversion for this check.
    decode := starlark.NewBuiltin("json.decode", func(t *starlark.Thread, b *starlark.Builtin, args starlark.Tuple, kwargs []starlark.Tuple) (starlark.Value, error) {
        converted := append(starlark.Tuple(nil), args...)
        if bytes, ok := converted[0].(starlark.Bytes); ok { converted[0] = starlark.String(bytes) }
        return starlark.Call(t, starjson.Module.Members["decode"], converted, kwargs)
    })
    globals, err := starlark.ExecFile(thread, os.Args[1], nil,
        starlark.StringDict{"json": &starlarkstruct.Module{Name:"json", Members:starlark.StringDict{"decode":decode}}})
    if err != nil { panic(err) }
    valid := `{"warbletCameraHealth":1,"camera":"unavailable","error":-1,"attempts":3,"uptime_s":120,"snap":"failed"}`
    cases := []struct { payload, tag, want string; bad bool }{
        {valid, "health", `"camera": "unavailable"`, false},
        {`{"warbletCameraHealth":1,"camera":"ready","error":0,"attempts":1,"uptime_s":42,"snap":"none"}`, "health", `"snap": "none"`, false},
        {`{"warbletCameraHealth":1,"camera":"stalled","error":263,"attempts":1,"uptime_s":42,"snap":"failed"}`, "health", `"camera": "stalled"`, false},
        {"\xff\xd8\xffimage", "snap", `"snap": "none"`, false},
        {"\xff\xd8\xffimage", "heartbeat", `"camera": "ready"`, false},
        {"ordinary", "other", `{"bytes": 8}`, false},
        {`[]`, "health", `{"bytes": 2}`, false},
        {`{"ordinary":1}`, "health", `{"bytes": 14}`, false},
        {`{ordinary`, "health", `{"bytes": 9}`, false},
        {`{"warbletCameraHealth":1}`, "health", "", true},
        {`{"warbletCameraHealth":1,"camera":"connected","error":0,"attempts":1,"uptime_s":42,"snap":"none"}`, "health", "", true},
        {`{"warbletCameraHealth":1,"camera":"ready","error":0,"attempts":4,"uptime_s":42,"snap":"none"}`, "health", "", true},
        {`{"warbletCameraHealth":1,"camera":"ready","error":0,"attempts":1,"uptime_s":-1,"snap":"none"}`, "health", "", true},
        {`{"warbletCameraHealth":1,"camera":"ready","error":0,"attempts":true,"uptime_s":42,"snap":"none"}`, "health", "", true},
    }
    for i, c := range cases {
        // Match current core/sandbox.go's actual Meta dictionary: no tag.
        meta := starlark.NewDict(5)
        meta.SetKey(starlark.String("hw_id"), starlark.String("test-camera"))
        meta.SetKey(starlark.String("transport"), starlark.String("mqtt"))
        meta.SetKey(starlark.String("ts_ms"), starlark.MakeInt(123))
        meta.SetKey(starlark.String("secret"), starlark.Bytes(""))
        meta.SetKey(starlark.String("ip"), starlark.String("127.0.0.1"))
        result, err := starlark.Call(thread, globals["decode"], starlark.Tuple{starlark.Bytes(c.payload), meta}, nil)
        if c.bad { if err == nil { panic(fmt.Sprintf("case %d accepted malformed health", i)) }; continue }
        if err != nil { panic(err) }
        if !contains(result.String(), c.want) { panic(fmt.Sprintf("case %d: %s missing %s", i, result, c.want)) }
    }
    fmt.Println("PASS: 14 decoder cases with actual core metadata (no tag): health, JPEG recovery, legacy payload, invalid health")
}
func contains(s, target string) bool {
    for i:=0; i+len(target)<=len(s); i++ { if s[i:i+len(target)] == target { return true } }
    return false
}
