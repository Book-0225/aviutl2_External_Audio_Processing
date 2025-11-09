# External Audio Processing 2 for AviUtl ExEdit2

**External Audio Processing 2**はAviUtl ExEdit2上でオーディオプラグイン（VST&reg;3 / CLAP）を利用可能にするためのフィルタプラグインです。

![VST COMPATIBLE LOGO](./vst_logo.png)

## 注意事項

- 無保証です。自己責任で使用してください。  
このプラグインを利用したことによる、いかなる損害・トラブルについても責任を負いません。

- すべてのVST3/CLAPプラグインとの完全な互換性を保証するものではありません。  
プラグインの実装によっては、GUIが正常に表示されない、動作が不安定になる、あるいはAviUtl2がクラッシュする可能性があります。  
不具合を発見した場合報告していただけるとありがたいです。

## 操作上の注意と既知の問題

### フィルタ設定の変更がリアルタイムに反映されない問題

このプラグインでは、設定パネルで行った操作が即座に反映されない場合があります。具体的には、以下のような状況が発生します。

- `プラグインGUIを表示` のチェックをONにしても、GUIウィンドウが表示されない。
- `プラグインGUIを表示` のチェックをOFFにしても、表示されているGUIウィンドウが消えない。
- フィルタをオブジェクトに初めて追加した際の内部的な初期化（ユニークIDの生成）が行われず、設定が正しく保存されない。

#### 原因

このプラグインは、AviUtl ExEdit2 Plugin SDKの仕様によりメインの処理関数が呼び出されるタイミングでのみ、設定パネルの変更を検知して実際の動作（GUIの表示/非表示など）を行います。

そして、このメインの処理関数が呼び出され、かつ、どのオブジェクトに対する操作かを正しく認識するためには、**以下の2つの条件が同時に満たされている**必要があります。

1. **再生カーソルがオブジェクトの範囲内にあること。** (これによりメインの処理関数が呼び出されます)
2. **そのオブジェクトが選択されていること。** (これにより、プラグインが操作対象のオブジェクトを特定できます)

これらの条件が満たされていない場合、設定パネルで値を変更しても、プラグインはそれに気づくことができず、何も実行しません。

#### 対処方法

このフィルタの設定を変更する際は、必ず以下の手順で操作してください。

1. タイムライン上で、設定したいオブジェクトをクリックして**選択状態**にします。
2. そのまま、**再生カーソルをそのオブジェクトの範囲内に移動**させます。
3. この状態を維持したまま、設定パネルの項目（`プラグインGUIを表示`など）を操作します。

**オブジェクトを選択し、次に再生カーソルをその上に乗せてから、設定を変更する**という操作が、このプラグインの現状の使い方となります。

### オブジェクトの先頭でノイズや音途切れが発生する問題

フィルタを適用したオブジェクトの再生開始時や、書き出したファイルの該当箇所で、

- 「プチッ」という短いノイズが聞こえる
- 音が途切れる

といった現象が発生することがあります。

#### 原因

オーディオプラグインは、そのオブジェクトで初めて音声処理を行う際に、読み込みと初期化に時間がかかる場合があります。

この初期化遅延が、オブジェクトの先頭部分での処理こぼしとなり、ノイズや音途切れとして現れます。

#### 対処方法

書き出しを行う前に、一度プロジェクト全体を**最初から最後まで再生**してください。

これにより、使用されている全てのオーディオプラグインが事前に読み込まれ、初期化が完了した状態になります。

その結果、書き出し時の処理遅延がなくなり、冒頭のノイズや音途切れを防ぐことができます。

## 動作要件

- AviUtl ExEdit2 version 2.00 beta19以降
  - https://spring-fragrance.mints.ne.jp/aviutl
  - バージョンアップによって仕様が変更された場合動作しない可能性があります。
  - 初代AviUtlでは[初代External_Audio_Processing](https://github.com/Book-0225/aviutl_External_Audio_Processing)を利用してください。

- Visual C++ 再頒布可能パッケージ（[2015/2017/2019/2022] の x64 対応版が必要）
  - https://learn.microsoft.com/ja-jp/cpp/windows/latest-supported-vc-redist

## インストール

1. リリースページから最新版の `External_Audio_Processing2.aux2` をダウンロードします。
2. `External_Audio_Processing2.aux2`ファイルを、AviUtl ExEdit2の`Plugin`フォルダ内に入れてください。

## 設定項目

- `プラグイン`:
  ファイル選択ボタンをクリックして、使用したいオーディオプラグインファイル(.vst3 または .clap) を選択します。  
  プラグインの適用をやめたい場合は、このパスを空欄にしてください。

- `プラグインGUIを表示`:
  チェックボックスをONにすると、選択したプラグイン専用の設定ウィンドウが表示されます。  
  パラメータを調整し終えたら、チェックをOFFにしてください。  
  (ウィンドウを閉じる際に、現在の設定状態が自動的にプロジェクトへ保存されます)

- `__INSTANCE_ID__`:
  プラグインの状態をプロジェクト内で一意に管理するためのIDです。  
  この値は自動的に割り当てられるため、ユーザーが編集する必要はありません。  
  v0.0.5にてAviUtl ExEdit2 Plugin SDKの更新に伴い廃止しましたが互換性のため残してあります。  
  v0.0.5以降のバージョンではこの項目の中身は空になります。

## 改版履歴

- **v0.0.6**
  - フィルタをコピーした際に正しく動作しない問題の修正
  - データ移行時に誤ったオブジェクトのIDを削除するなどの移行時に関する問題を複数修正

- **v0.0.5**
  - 動作に必要なAviUtl ExEdit2のバージョンをv2.00 beta19以降に変更
  - AviUtl ExEdit2 Plugin SDKの更新に伴いインスタンスIDの管理を移行  
    (しばらくの間はv0.0.4以前からのデータ移行ができるようにしておきます)
  - VST3プラグインのパラメータが保存されないことがあった問題の修正
  - シーク後の音声にシーク前の音声が影響していたことがあった問題の修正

- **v0.0.4**
  - インスタンスIDの割り当て処理を改善
  - 互換性のない状態を読み込んでいたことによるプラグイン切り替え時のクラッシュを修正

- **v0.0.3**
  - プレビュー再生が一時停止する問題を修正

- **v0.0.2**
  - 同じオブジェクトに複数のフィルタを適用した際に正しく動作しない問題を修正

- **v0.0.1**
  - 初版

## License

このプロジェクトはMIT Licenseの下で公開されています

## ビルド方法

### 前提条件

- Visual Studio 2022
- cmake
- Git

### 実際の手順

1. `git clone --recursive https://github.com/Book-0225/aviutl2_External_Audio_Processing.git`
2. `cd aviutl2_External_Audio_Processing`
3. `aviutl2_External_Audio_Processing/aviutl2_sdk`になるようにaviutl2のSDKを配置
4. `mkdir vst3sdk_build`
5. `cd .\vst3sdk_build\`
6. `cmake ..\vst3sdk\`
7. `cmake --build . --config Release`
8. `cd ..`
9. `msbuild /p:Configuration=Release /p:Platform="x64"`

上記の通り実行すると`x64/Release/External_Audio_Processing2.aux2`が生成されるはずです。

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
