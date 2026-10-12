# third_party/patches/ — our changes to upstream code, as patch series

| upstream | base | patches | where they live |
|---|---|---|---|
| [pcrecomp](https://github.com/sp00nznet/pcrecomp) | `1f49cea` | 3 (lift32 x87/flags fixes) | commits on the `third_party/pcrecomp` submodule |
| [MojoShader](https://github.com/icculus/mojoshader) | `ad5dff8` | 3 (D3D8 vs.1.x, GLSL a0) | commits on the `third_party/mojoshader` submodule |
| [d3d9-webgl](https://github.com/LostMyCode/d3d9-webgl) | `9f2543a` | 8 | history only: the renderer is now `render/webgl/` (ours) |

The submodules point at our patched commits, which exist only locally (nothing is pushed). To rebuild a
submodule from upstream: `git -C third_party/NAME checkout BASE && git -C third_party/NAME am ../patches/NAME/*.patch`.
After changing a submodule, regenerate its series: `git -C third_party/NAME format-patch -o ../patches/NAME BASE..HEAD`
(`./init.sh structure` checks that every commit of ours has its patch).
