# BC-ResNet1 8-class 模型预检

此预检仅构建 `tflm_benchmark`，不改变 4-class `audio_event` 主应用。

模型来源：`/home/arongw/Documents/audio/micro_model/output/bcresnet1_8class/model_int8.tflite`。
导入脚本只接受 SHA-256 为
`37834bb5beb39d1872dd279a8c99b1feff0d071e780ec5c6fc29a15a83731148` 的重新校准模型。

预检 resolver 注册 14 种模型算子：RELU、STRIDED_SLICE、PACK、RESHAPE、CONV_2D、
DEPTHWISE_CONV_2D、MUL、ADD、CONCATENATION、MEAN、LOGISTIC、SPACE_TO_BATCH_ND、
BATCH_TO_SPACE_ND、FULLY_CONNECTED。

## 使用步骤

```bash
./ccf_audioevent/scripts/import_bcresnet1_8class_model.sh \
  /home/arongw/Documents/audio/micro_model/output/bcresnet1_8class/model_int8.tflite
./build.sh ccf_audioevent/board/esp32s3-devkit/configs/tflm_benchmark -j8
```

烧录后先运行：

```bash
tflm_benchmark --warmup 0 --repeat 1 --csv
```

成功标准是出现 `[bcresnet-preflight] arena=... used=...`，并完成一次 Invoke。输出值是
Softmax 之前的量化输出，仅用于模型运行与量化参数预检，不能当作概率或检测阈值依据。
