# External Audio Processing 2 for AviUtl ExEdit2

**External Audio Processing 2**はAviUtl ExEdit2上でオーディオプラグイン（VST&reg;3 / CLAP）を利用可能にするためのフィルタプラグインです。

![VST COMPATIBLE LOGO](./vst_logo.png)

## 注意事項

- 無保証です。自己責任で使用してください。  
このプラグインを利用したことによる、いかなる損害・トラブルについても責任を負いません。

- すべてのVST3/CLAPプラグインとの完全な互換性を保証するものではありません。  
プラグインの実装によっては、GUIが正常に表示されない、動作が不安定になる、あるいはAviUtl2がクラッシュする可能性があります。  
不具合を発見した場合報告していただけるとありがたいです。

- v0.0.xの内は開発初期のリリースです。  
致命的な不具合や破壊的変更などが起こりやすいと思われます。  
使用する際はバックアップを取っておくことをおすすめします。

- V0.0.1xでは、VST3プラグインの更新を優先して行っていますので、
  CLAPプラグインの更新が遅れています。
  VST3プラグインの更新がある程度完了したら、CLAPプラグインの更新も順次行う予定です。
  それまでの間CLAPプラグインで一部の機能が使えない可能性や不安定な動作が発生する可能性があります。

## 推奨事項

このプラグインはVST3プラグインの利用を主機能としたものです。  
VST3プラグインをお持ちでない場合や、AviUtl ExEdit2内でより手軽に本格的な音声加工を行いたい場合は、  
**うつぼかずら様**の制作された以下のプラグインを導入することを強くおすすめします。

- **Utsbox Audio Effect for AviUtl ExEdit2**
  - <https://www.utsbox.com/>

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

- AviUtl ExEdit2 version 2.00 beta22以降
  - <https://spring-fragrance.mints.ne.jp/aviutl>
  - バージョンアップによって仕様が変更された場合動作しない可能性があります。
  - 初代AviUtlでは[初代External_Audio_Processing](https://github.com/Book-0225/aviutl_External_Audio_Processing)を利用してください。

- Visual C++ 再頒布可能パッケージ（v14 の x64 対応版が必要）
  - <https://learn.microsoft.com/ja-jp/cpp/windows/latest-supported-vc-redist>

## インストール

1. リリースページから最新版の `External_Audio_Processing2.aux2` をダウンロードします。
2. `External_Audio_Processing2.aux2`ファイルを、AviUtl ExEdit2の`Plugin`フォルダ内に入れてください。

## 設定項目

### メインフィルタ

#### メインフィルタについての注意

VST3プラグインのみで動く機能など、機能によってはどちらかのプラグインでしか正常に動作しないものがあります。

#### External Audio Processing 2 (Host)

##### General Settings

- `プラグイン`:  
  ファイル選択ボタンをクリックして、使用したいオーディオプラグインファイル(.vst3 または .clap) を選択します。  
  プラグインの適用をやめたい場合は、このパスを空欄にしてください。

- `Wet`(v0.0.10で追加)(フィルタとして利用する場合のみ):  
  オーディオプラグインの出力音声と、元の音声の比率を調整します。  
  プラグインの出力音声に対してのみ適用されます。  
  MIDIファイルを用いたVSTiプラグインの場合、この値を100にすると原音が消えると思うので、その場合は値を下げて調整してください。

- `Gain`(v0.0.10で追加)(フィルタとして利用する場合のみ):  
  オーディオプラグインの出力音声のゲインを調整します。  
  プラグインの出力音声に対してのみ適用されます。

- `BPM`(v0.0.11で追加):  
  VST3プラグインに対して送信するBPMを指定します。  
  また、MIDIファイルの再生速度を指定する際にも使用されます。

- `分子`(v0.0.11で追加):  
  VST3プラグインに対して送信する拍子の分子を指定します。

- `分母`(v0.0.11で追加):  
  VST3プラグインに対して送信する拍子の分母を指定します。

- `Apply to L`(v0.0.10で追加)(フィルタとして利用する場合のみ):  
  出力音声の左チャンネルに対して適用します。  
  プラグインの出力音声に対してのみ適用されます。

- `Apply to R`(v0.0.10で追加)(フィルタとして利用する場合のみ):  
  出力音声の右チャンネルに対して適用します。  
  プラグインの出力音声に対してのみ適用されます。

- `プラグインGUIを表示`:  
  チェックボックスをONにすると、選択したプラグイン専用の設定ウィンドウが表示されます。  
  パラメータを調整し終えたら、チェックをOFFにしてください。  
  (ウィンドウを閉じる際に、現在の設定状態が自動的にプロジェクトへ保存されます)

##### Parameter Settings

- `Learn Param`(alpha版):  
  VST3プラグインのパラメータと下記`Param 1 ~ 4`の値を紐づけます。  
  このチェックボックスにチェックを入れた後、紐づけたい`Param`の値を変更してから、  
  VST3プラグインのパラメータを変更して、再度紐づけたい`Param`の値を変更してください。  
  その後`Learn Param`のチェックを外してください。  
  そうすると、`Param 1 ~ 4`の値がVST3プラグインのパラメータに反映されます。  
  この設定方法はわかりずらい上、うまくいかないこともあるので変更を予定しています。

- `Reset Mapping`(alpha版):  
  `Learn Param`で紐づけた`Param 1 ~ 4`の紐づけを全て解除します。

- `Param 1 ~ 4`(alpha版):  
  VST3プラグインのパラメータと紐づけることができるパラメータです。  
  `Learn Param`の説明に従って紐づけた後、移動などを設定してください。

##### MIDI Settings

- `MIDI File`(alpha版):  
  VSTiプラグインに対して送信するMIDIファイルを指定します。  
  MIDIファイルを指定することで、VSTiプラグインに対してMIDIイベントを送信することができます。

- `MIDIにBPMを同期`(alpha版):  
  MIDIファイルのテンポ情報をBPMに同期します。
  トラックバーのBPMを無視します。

##### Legacy Settings

- `__INSTANCE_ID__`（フィルタとして利用する場合のみ）:  
  プラグインの状態をプロジェクト内で一意に管理するためのIDです。  
  この値は自動的に割り当てられるため、ユーザーが編集する必要はありません。  
  v0.0.5にてAviUtl ExEdit2 Plugin SDKの更新に伴い廃止しましたが互換性のため残してあります。  
  v0.0.5以降のバージョンで一度プレビューすると、データが移行されこの項目は空になります。  
  v0.0.13で追加されたメディアオブジェクト版には最初からこの項目がありません。  
  フィルタの場合も廃止予定のためこの項目が空でない場合は、最新版を用いて一度プレビューし、  
  データを移行して保存しなおすことをおすすめします。

### 内蔵メディアオブジェクト

#### 内蔵メディアオブジェクトについての注意

私は音声編集に詳しくありませんので、内蔵メディアオブジェクトの正確な動作を保証することはできません。  
また、内蔵メディアオブジェクトは試験的であり、安定性や正確性を保証することはできません。

#### External Audio Processing 2 Generator

- `Waveform`:  
  `Sine`,`Square`,`Triangle`,`Saw`,`White Noise`,`Pink Noise`のいずれかを指定します。
- `Frequency`:  
  生成する波形の周波数を指定します。

### 内蔵フィルタ (alpha版)

#### 内蔵フィルタについての注意

私は音声編集に詳しくありませんので、内蔵フィルタの正確な動作を保証することはできません。  
また、内蔵フィルタは試験的であり、安定性や正確性を保証することはできません。

#### External Audio Processing 2 Utility

- `Gain`:  
  出力音声のゲインを調整します。

- `Pan(L-R)`:  
  出力音声のパンを調整します。

- `Swap(L/R)`:  
  出力音声の左右を反転します。

- `Invert L`:  
  出力音声の左チャンネルを位相反転します。

- `Invert R`:  
  出力音声の右チャンネルを位相反転します。

- `Mono Mode`(v0.0.14で変更):  
  `Stereo (Off)`,`Mix (L+R)`,`Left to Stereo`,`Right to Stereo`のいずれかを指定します。

#### External Audio Processing 2 EQ

##### Cut Filters

- `Low Cut`:  
  出力音声をローパスフィルタでカットする周波数を指定します。

- `High Cut`:  
  出力音声をハイパスフィルタでカットする周波数を指定します。

##### EQ Bands

- `Low Gain`:  
  出力音声の低域のゲインを調整します。

- `M-Low Gain`:  
  出力音声の中低域のゲインを調整します。

- `Mid Gain`:  
  出力音声の中域のゲインを調整します。

- `M-High Gain`:  
  出力音声の中高域のゲインを調整します。

- `High Gain`:  
  出力音声の高域のゲインを調整します。

- `Low Freq`:  
  出力音声の低域の周波数を指定します。

- `M-Low Freq`:  
  出力音声の中低域の周波数を指定します。

- `Mid Freq`:  
  出力音声の中域の周波数を指定します。

- `M-High Freq`:  
  出力音声の中高域の周波数を指定します。

- `High Freq`:  
  出力音声の高域の周波数を指定します。

#### External Audio Processing 2 Stereo

- `Width`:  
  出力音声の広がりを調整します。

- `Mid Level`:  
  出力音声の中域のゲインを調整します。

- `Side Level`:  
  出力音声の周域のゲインを調整します。

#### External Audio Processing 2 Dynamics

##### Gate Settings

- `Gate Threshold`:  
  出力音声のノイズゲートの閾値を調整します。

- `Gate Attack`:  
  効果が掛かり始めるまでの時間を調整します。

- `Gate Release`:  
  効果が切れるまでの時間を調整します。

##### Compressor Settings

- `Comp Threshold`:  
  出力音声のコンプレッサーの閾値を調整します。

- `Comp Ratio`:  
  出力音声のコンプレッサーの比率を調整します。

- `Comp Attack`:  
  効果が掛かり始めるまでの時間を調整します。

- `Comp Release`:  
  効果が切れるまでの時間を調整します。

- `Makeup Gain`:  
  出力音声のコンプレッサーの補正ゲインを調整します。

- `Limiter`:  
  出力音声のリミッター閾値を調整します。

#### External Audio Processing 2 Spatial

- `Delay Time`:  
  出力音声のディレイ時間を調整します。

- `Feedback`:  
  出力音声のディレイのフィードバックを調整します。

- `Delay Mix`:  
  出力音声のディレイの割合を調整します。

- `Pseudo Width`:  
  出力音声を疑似ステレオ化する割合を調整します。

#### External Audio Processing 2 Modulation

- `Chorus`:  
  出力音声のコーラスを設定します。

- `Flanger`:  
  出力音声のフランジャーを設定します。

- `Tremolo`:  
  出力音声のトレモロを設定します。

- `Rate`:  
  出力音声のモジュレーションの周波数を調整します。

- `Depth`:  
  出力音声のモジュレーションの深さを調整します。

- `Feedback`:  
  出力音声のモジュレーションのフィードバックを調整します。

- `Delay`:  
  出力音声のモジュレーションのディレイ時間を調整します。

- `Mix`:  
  出力音声のモジュレーションの割合を調整します。

#### External Audio Processing 2 Distortion

- `Overdrive`:  
  出力音声のオーバードライブを設定します。

- `Fuzz`:  
  出力音声のファズを設定します。

- `Bitcrush`:  
  出力音声のビットクラッシャーを設定します。

- `Drive`:  
  出力音声のドライブを調整します。

- `Tone`:  
  出力音声のトーンを調整します。

- `Bits`:  
  出力音声のビット数を調整します。

- `Downsample`:  
  出力音声のダウンサンプリングを設定します。

- `Mix`:  
  出力音声の割合を調整します。

- `Output`:  
  出力音声のゲインを調整します。

#### External Audio Processing 2 Maximizer

- `Threshold`:  
  出力音声のマキシマイザーの閾値を調整します。

- `Ceiling`:  
  出力音声のマキシマイザーの最大値を調整します。

- `Release`:  
  効果が切れるまでの時間を調整します。

- `Lookahead`:  
  出力音声のマキシマイザーの先読み時間を調整します。

#### External Audio Processing 2 Reverb

- `Room Size`:  
  出力音声のリバーブの部屋サイズを調整します。

- `Damping`:  
  出力音声のリバーブのダンピングを調整します。

- `Wet`:  
  出力音声のリバーブのゲインを調整します。

- `Mix`:  
  出力音声のリバーブの割合を調整します。

#### External Audio Processing 2 Phaser

- `Rate`:  
  出力音声のフェイザーの周波数を調整します。

- `Depth`:  
  出力音声のフェイザーの深さを調整します。

- `Feedback`:  
  出力音声のフェイザーのフィードバックを調整します。

- `Mix`:  
  出力音声のフェイザーの割合を調整します。

#### External Audio Processing 2 Pitch Shift

- `Pitch`:  
  出力音声のピッチを調整します。

- `Mix`:  
  出力音声のピッチシフトの割合を調整します。

#### External Audio Processing 2 De-esser

- `Frequency`:  
  検知・抑制する周波数帯域の中心を指定します。

- `Threshold`:  
  抑制を開始する閾値を調整します。

- `Amount`:  
  抑制する最大量を調整します。

- `Width(Q)`:  
  抑制する帯域の広さを調整します。

#### External Audio Processing 2 Auto Wah

- `Sensitivity`:  
  入力音声に対する感度を調整します。

- `Base Freq`:  
  フィルタの基準となる周波数を指定します。

- `Range`:  
  フィルタが動く範囲を指定します。

- `Resonance`:  
  フィルタのレゾナンスを調整します。

- `Mix`:  
  原音とエフェクトの割合を調整します。

### チェインフィルタ

SendとCompの両方が同じIDを指定することでチェインフィルタとして動作します。  
同じIDでSendを複数作成することはできませんが、複数のオブジェクトで受信することは可能です。

#### External Audio Processing 2 Chain Send

- `ID`:  
  チェイン送信のIDを設定します。

- `Send Gain`:  
  チェイン送信のゲインを調整します。

#### External Audio Processing 2 Chain Comp

- `ID`:  
  受信するチェインIDを設定します。

- `Threshold`:  
  コンプレッサーの閾値を調整します。

- `Ratio`:  
  コンプレッサーの比率を調整します。

- `Attack`:  
  効果が掛かり始めるまでの時間を調整します。

- `Release`:  
  効果が切れるまでの時間を調整します。

- `Makeup Gain`:  
  補正ゲインを調整します。

#### External Audio Processing 2 Chain Gate

- `ID`:  
  受信するチェインIDを設定します。

- `Threshold`:  
  ゲートの閾値を調整します。

- `Ratio`:  
  ゲートの比率を調整します。

- `Attack`:  
  効果が掛かり始めるまでの時間を調整します。

- `Release`:  
  効果が切れるまでの時間を調整します。

#### External Audio Processing 2 Chain Dynamic EQ

- `ID`:  
  受信するチェインIDを設定します。

- `Frequency`:  
  ゲインを下げる中心周波数を指定します。

- `Q`:  
  影響を与える帯域幅を調整します。

- `Reduction`:  
  最大で何dB下げるかを指定します。

- `Threshold`:  
  効果が掛かり始める音量を指定します。

- `Attack`:  
  効果が掛かり始めるまでの時間を調整します。

- `Release`:  
  効果が切れるまでの時間を調整します。

#### External Audio Processing 2 Chain Filter

Chain Sendから送られてきた音量に応じて、ローパスフィルタのカットオフ周波数を動かします。
ドラムのキックに合わせてベースの音色を変えるなどの演出に使用できます。

- `ID`:  
  受信するチェインIDを設定します。

- `Base Freq`:  
  基準となるカットオフ周波数を指定します。

- `Mod Depth`:  
  音量に応じて周波数をどれくらい動かすかを指定します。

- `Resonance`:  
  フィルタのレゾナンス（癖の強さ）を調整します。

- `Attack`:  
  フィルタが動き始めるまでの時間を調整します。

- `Release`:  
  フィルタが元の位置に戻るまでの時間を調整します。

## 改版履歴

- **v0.0.15**
  - アタック時間とリリース時間の計算ミスを修正
  - DynamicsのGateにアタック時間の調整項目を追加
  - Dynamicsのクラッシュ問題を修正

- **v0.0.14**
  - 一部処理にAVX2を導入  
    これにより一部のコンピューターで動作しない可能性があります  
    AviUtl ExEdit2本体がAVX2を要求するため  
    このプラグインの有無による問題は起こらないはずですが、  
    それでも問題が発生した場合は、報告してください
  - チェインフィルタプラグインを仮追加(alpha版)  
    これらのプラグインはまだ破壊的変更を伴うアップデートの可能性があります
    - External Audio Processing 2 Chain Send
      - 音量情報を送るだけのプラグイン(音声に変更は加えません)
    - External Audio Processing 2 Chain Comp
      - 送られてきた音量情報を元に動作するコンプレッサー
    - External Audio Processing 2 Chain Gate
      - 送られてきた音量情報を元に動作するゲート
    - External Audio Processing 2 Chain Dynamic EQ
      - 送られてきた音量情報を元に動作するEQ
    - External Audio Processing 2 Chain Filter
      - 送られてきた音量情報を元に動作するローパスフィルタ
  - メディアオブジェクトを仮追加(alpha版)
    - External Audio Processing 2 Generator
      - 波形ジェネレータ
  - フィルタを仮追加(alpha版)
    - External Audio Processing 2 Reverb
      - リバーブ
    - External Audio Processing 2 Phaser
      - フェイザー
    - External Audio Processing 2 Pitch Shift
      - ピッチシフター
    - External Audio Processing 2 Auto Wah
      - ワウ
    - External Audio Processing 2 DeEsser
      - ディエッサー
  - External Audio Processing 2 UtilityのMonoMixをMonoModeに変更  
    (v0.0.13までとは互換性がありません)
  - フィルタが増えてきたため、ラベルを音声効果からEAP2に変更  
    これはすでに一度インストール済みの環境には自動適用されないので、  
    オブジェクト追加メニューの設定より手動で変更してください
  - CLAPを1.2.7に更新(コード自体のメンテナンスはしていないので、動作は保証できません)
  - VST3プラグインのGUI更新に関する問題の修正
  - 動作に必要なAviUtl ExEdit2のバージョンをv2.00 beta22以降に変更
  - 一部プラグインの項目をグループ化
  - Distortionのクラッシュ問題を修正
  - EQのノイズ問題を修正
  - 使用しているVisual Studioのバージョンを2026に変更

- **v0.0.13**
  - メディアオブジェクトとして利用できるように変更(VSTiを使う場合を想定した機能です)
  - MIDIファイルのテンポ情報をBPMに同期する機能を追加
  - プラグインウィンドウで機能していなかった最大化などのボタンを削除(最小化したい場合はGUIを閉じてください)

- **v0.0.12**
  - VST3プラグインに対し、送信する情報が絶対位置になっていた問題の修正
  - プラグインの不必要なリセットが発生していた問題の修正
  - MIDIファイルを用いたVSTiプラグインへの仮対応(alpha版)

- **v0.0.11**
  - VST3プラグインに対して送信する情報の追加
  - VST3プラグインに対し一部のnullptrを渡していた部分をダミーデータに変更し、クラッシュを改善
  - 重大なエラーが起きてもクラッシュせずにできるだけの機能を提供するように改善
  - データの簡易破損チェックを実装して破損したデータを読み込まないように改善

- **v0.0.10**
  - コード全体の大幅な改善(下記の拡張への対応が主です)
  - VSTやCLAPを用いることなく音声編集が可能になるプラグインを仮追加(alpha版)  
    これらのプラグインはまだ破壊的変更を伴うアップデートの可能性があります
    - External Audio Processing 2 Utility
      - ゲイン、パン、左右反転、位相反転、モノラル化
    - External Audio Processing 2 EQ
      - 5バンドEQ、ローパス/ハイパスフィルタ
    - External Audio Processing 2 Stereo
      - 音の広がり、ボーカルと伴奏のバランス
    - External Audio Processing 2 Dynamics
      - コンプレッサー、ノイズゲート、リミッター
    - External Audio Processing 2 Spatial
      - ディレイ、疑似ステレオ
    - External Audio Processing 2 Modulation
      - コーラス、フランジャー、トレモロ
    - External Audio Processing 2 Distortion
      - オーバードライブ、ファズ、ビットクラッシャー
    - External Audio Processing 2 Maximizer
      - マキシマイザー
  - オーディオプラグインのウェット、ゲイン、L/Rのみ適用機能を追加  
    これらの項目はオーディオプラグインそのものには反映されず、  
    プラグインの出力音声に対してのみ適用されます
  - ログ出力をAviUtl ExEdit2本体の機能に移行
  - VST3プラグインに限りパラメータの変更をAviUtl ExEdit2側から行うことができるようになりました(alpha版)
  - 動作に必要なAviUtl ExEdit2のバージョンをv2.00 beta21以降に変更

- **v0.0.9**
  - オーディオプラグイン設定の保存時にデータが破損していた問題の修正
  - 上記の問題などによって破損した設定の簡易復元機能の追加
  - フィルタ情報の表示を変更(動作に影響はありません)
  - 内部のファイルパスの扱いを改善

- **v0.0.8**
  - 音声処理パフォーマンスの改善
  - Visual Studio 2026に仮対応(まだ動作確認がとり終わっていません)

- **v0.0.7**
  - 保存時のフリーズ問題と不必要なリソース消費問題の修正

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

- Visual Studio 2026
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
9. `msbuild aviutl2_External_Audio_Processing.vcxproj /p:Configuration=Release /p:Platform="x64"`

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
