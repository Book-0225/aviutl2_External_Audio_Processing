[← README](../README.md)

# ビルド方法

## 情報

ビルド確認環境は以下の通りです。

- CMake: 4.4.4
- Ninja: 1.13.2
- GCC: 16.2.0
- llvm-mingw: 23.1.5
- VS2026: 18.11.3

全ての環境でのビルドが成功することを保証するものではありません。

## 前提条件

- CMake 4.3以降
- Git
- aviutl2 CLI
- pwsh(リリースノートを生成する場合のみ)
- C++23に対応した各種ツールチェーン(どれか一つ以上)
  - GCC: MinGW-w64
  - Clang: llvm-mingw
  - MSVC: Visual Studio 2026(C++によるデスクトップ開発)

## ビルド手順(Ninja)

- 上記前提条件に加えてNinjaが必要です

1. `git clone --recursive https://github.com/Book-0225/aviutl2_External_Audio_Processing.git`
2. `cd aviutl2_External_Audio_Processing`
3. `pwsh  -ExecutionPolicy Bypass -File ./scripts/setup_sdk.ps1`
4. `cmake -S ./ -B ./build -DCMAKE_BUILD_TYPE:STRING=Release -Wno-unused-cli -G "Ninja`
5. `au2 release`

上記の通り実行すると`build/bin/External_Audio_Processing2.aux2`と`release/External_Audio_Processing2.au2pkg.zip`が生成されるはずです。

コンパイラを指定する場合は手順4で`-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`のように指定してください。

## ビルド手順(Visual Studio 2026)

- MSVC専用です

1. `git clone --recursive https://github.com/Book-0225/aviutl2_External_Audio_Processing.git`
2. `cd aviutl2_External_Audio_Processing`
3. `pwsh  -ExecutionPolicy Bypass -File ./scripts/setup_sdk.ps1`
4. `cmake -S ./ -B ./build -Wno-unused-cli -G "Visual Studio 18 2026`
5. `au2 release`

上記の通り実行すると`build/bin/External_Audio_Processing2.aux2`と`release/External_Audio_Processing2.au2pkg.zip`が生成されるはずです。
