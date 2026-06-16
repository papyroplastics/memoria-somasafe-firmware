import numpy as np
from ai_edge_litert.interpreter import Interpreter


def dequantize(values, scale: float, zero_point: int) -> np.ndarray:
    """Dequantize an array of ints back to float using LiteRT scale/zero-point."""
    return (np.asarray(values, dtype=np.float32) - zero_point) * scale


class QuantTensor:
    """Quantization parameters and shape of a single model tensor."""

    def __init__(self, scale: float, zero_point: int, shape):
        self.scale = scale
        self.zero_point = zero_point
        self.shape = tuple(int(d) for d in shape)

    @property
    def size(self) -> int:
        return int(np.prod(self.shape))

    def dequantize(self, values) -> np.ndarray:
        return dequantize(values, self.scale, self.zero_point)


class ModelQuant:
    """Input/output quantization parameters of a quantized .tflite model."""

    def __init__(self, model_bytes: bytes):
        interpreter = Interpreter(model_content=model_bytes)
        in_detail = interpreter.get_input_details()[0]
        out_detail = interpreter.get_output_details()[0]

        in_scale, in_zero_point = in_detail['quantization']
        out_scale, out_zero_point = out_detail['quantization']

        self.input = QuantTensor(in_scale, in_zero_point, in_detail['shape'])
        self.output = QuantTensor(out_scale, out_zero_point, out_detail['shape'])

    @property
    def batch_size(self) -> int:
        return self.input.shape[0]

    @property
    def n_features(self) -> int:
        return self.input.shape[1]
