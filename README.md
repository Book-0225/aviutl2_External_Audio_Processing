# External Audio Processing 2 for AviUtl ExEdit2

**External Audio Processing 2**はAviUtl ExEdit2上でオーディオプラグイン（VST&reg;3 / CLAP）を利用可能にするためのフィルタプラグインです。

![VST COMPATIBLE LOGO](./vst_logo.png)

## 注意事項

- 無保証です。自己責任で使用してください。  
このプラグインを利用したことによる、いかなる損害・トラブルについても責任を負いません。

- すべてのVST3/CLAPプラグインとの完全な互換性を保証するものではありません。  
プラグインの実装によっては、GUIが正常に表示されない、動作が不安定になる、あるいはAviUtl2がクラッシュする可能性があります。  
不具合を発見した場合報告していただけるとありがたいです。

## 動作要件

- AviUtl ExEdit2 version 2.00 beta18b以降
  - https://spring-fragrance.mints.ne.jp/aviutl
  - バージョンアップによって仕様が変更された場合動作しない可能性があります。
  - 初代AviUtlでは[初代External_Audio_Processing](https://github.com/Book-0225/aviutl_External_Audio_Processing)を利用してください。

- Visual C++ 再頒布可能パッケージ（[2015/2017/2019/2022] の x64 対応版が必要）
  - https://learn.microsoft.com/ja-jp/cpp/windows/latest-supported-vc-redist

## インストール

1. リリースページから最新版の `External_Audio_Processing2.aux2` をダウンロードします。
2. `External_Audio_Processing2.aux2`ファイルを、AviUtl ExEdit2の`Plugin`フォルダ内に入れてください。

## 設定項目

- ```プラグイン```:
  ファイル選択ボタンをクリックして、使用したいオーディオプラグインファイル(.vst3 または .clap) を選択します。  
  プラグインの適用をやめたい場合は、このパスを空欄にしてください。

- ```プラグインGUIを表示```:
  チェックボックスをONにすると、選択したプラグイン専用の設定ウィンドウが表示されます。  
  パラメータを調整し終えたら、チェックをOFFにしてください。  
  (ウィンドウを閉じる際に、現在の設定状態が自動的にプロジェクトへ保存されます)

- ```__INSTANCE_ID__```:
  プラグインの状態をプロジェクト内で一意に管理するためのIDです。  
  この値は自動的に割り当てられるため、ユーザーが編集する必要はありません。

## 改版履歴

- **v0.0.1**
  - 初版。

## License

このプロジェクトはMIT Licenseの下で公開されています

## ビルド方法

### 前提条件

- Visual Studio 2022
- cmake
- Git

### 実際の手順

1. ```git clone --recursive https://github.com/Book-0225/aviutl2_External_Audio_Processing.git```
2. ```cd aviutl2_External_Audio_Processing```
3. ```aviutl2_External_Audio_Processing/aviutl2_sdk```になるようにaviutl2のSDKを配置
4. ```mkdir vst3sdk_build```
5. ```cd .\vst3sdk_build\```
6. ```cmake ..\vst3sdk\```
7. ```cmake --build . --config Release```
8. ```cd ..```
9. ```msbuild /p:Configuration=Release /p:Platform="x64"```

上記の通り実行すると```x64/Release/External_Audio_Processing2.aux2```が生成されるはずです。

## Credits

### AviUtl ExEdit2 Plugin SDK

```
---------------------------------
AviUtl ExEdit2 Plugin SDK License
---------------------------------

The MIT License

Copyright (c) 2025 Kenkun

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

### VST SDK 3

**VST is a registered trademark of Steinberg Media Technologies GmbH.**

```
//-----------------------------------------------------------------------------
MIT License

Copyright (c) 2025, Steinberg Media Technologies GmbH

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

//---------------------------------------------------------------------------------
```

### CLAP

```
MIT License

Copyright (c) 2021 Alexandre BIQUE

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
