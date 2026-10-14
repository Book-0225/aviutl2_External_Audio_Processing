[← README](../README.md)

# チェインフィルタ

SendとCompの両方が同じIDを指定することでチェインフィルタとして動作します。  
複数のオブジェクトで送受信することが可能です。(v0.0.16以降)  
送信元と送信先は同じシーン内である必要があります。

## External Audio Processing 2 Chain Send

- `ID`:  
  チェイン送信のIDを設定します。

- `Send Gain`:  
  チェイン送信のゲインを調整します。

- `Offset`:  
  オブジェクト先頭から音声生成を開始するまでの時間（秒）を調整します。

- `Duration`:  
  音声を生成する継続時間（秒）を調整します。  
  0 の場合はオフセット以降オブジェクト末尾まで生成します。

## External Audio Processing 2 Chain Comp

- `ID`:  
  受信するチェインIDを設定します。

- `Threshold`:  
  コンプレッサーのしきい値を調整します。

- `Ratio`:  
  コンプレッサーの比率を調整します。

- `Attack`:  
  効果が掛かり始めるまでの時間を調整します。

- `Release`:  
  効果が切れるまでの時間を調整します。

- `Makeup Gain`:  
  補正ゲインを調整します。

## External Audio Processing 2 Chain Gate

- `ID`:  
  受信するチェインIDを設定します。

- `Threshold`:  
  ゲートのしきい値を調整します。

- `Ratio`:  
  ゲートの比率を調整します。

- `Attack`:  
  効果が掛かり始めるまでの時間を調整します。

- `Release`:  
  効果が切れるまでの時間を調整します。

## External Audio Processing 2 Chain Dynamic EQ

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

## External Audio Processing 2 Chain Filter

Chain Sendから送られてきた音量に応じて、ローパスフィルタのカットオフ周波数を動かします。
ドラムのキックに合わせてベースの音色を変えるなどの演出に使用できます。

- `ID`:  
  受信するチェインIDを設定します。

- `Base Freq`:  
  基準となるカットオフ周波数を指定します。

- `Mod Depth`:  
  音量に応じて周波数をどれくらい動かすかを指定します。

- `Resonance`:  
  フィルタのレゾナンスを調整します。

- `Attack`:  
  フィルタが動き始めるまでの時間を調整します。

- `Release`:  
  フィルタが元の位置に戻るまでの時間を調整します。
