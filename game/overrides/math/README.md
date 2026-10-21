# game/overrides/math/ — the game's hot math in C

Vector and matrix helpers the game calls thousands of times a frame, written in C with the lifted x87
code's exact arithmetic (same results bit for bit, checked by the deterministic state hashes).

- `vec3_transform.c` FUN_0079B230: a point through a 4x4 matrix (CPU skinning's inner call).
- `matrix_multiply.c` FUN_007BA0D6: D3DXMatrixMultiply (x87 path), the hottest function in a match.
