"""Read the ``.ssds`` subject export (``somasafe.capture.CaptureDataset``) written by the backend's ``scripts.system.export_subject_data``, used by the sensor and phone test-harness scripts to replay a subject's signal and reference features."""

from pathlib import Path

import numpy as np

from shared.gen.code import dataset_pb2 as pb

FORMAT_VERSION = 1


class CaptureExport:
    """One subject's capture export, indexed by window sequence number."""

    def __init__(self, path: Path):
        dataset = pb.CaptureDataset()
        dataset.ParseFromString(Path(path).read_bytes())
        if dataset.format_version != FORMAT_VERSION:
            raise ValueError(f"{path}: unsupported .ssds version "
                             f"{dataset.format_version} (expected {FORMAT_VERSION})")

        self.path = Path(path)
        self.subject = dataset.subject
        self.windows = {w.sequence_n: w for w in dataset.windows}
        self.norm_mean = np.frombuffer(dataset.norm_mean, dtype='<f4')
        self.norm_std = np.frombuffer(dataset.norm_std, dtype='<f4')

    def features(self, sequence_n: int) -> np.ndarray | None:
        window = self.windows.get(sequence_n)
        if window is None or not window.features:
            return None
        return np.frombuffer(window.features, dtype='<f4')

    def label(self, sequence_n: int) -> int | None:
        window = self.windows.get(sequence_n)
        if window is None or not window.score:
            return None
        return int(np.frombuffer(window.score, dtype=np.int8)[0])

    def signal_stream(self) -> tuple[np.ndarray, np.ndarray]:
        """Rebuild the recording's contiguous PPG and ACC streams from its windows, in sequence order."""
        sequences = sorted(self.windows)
        if sequences != list(range(len(sequences))):
            raise ValueError(
                f"{self.path}: window sequence is not gapless (got {len(sequences)} "
                f"windows spanning 0..{sequences[-1] if sequences else -1}); re-export "
                f"without --missing-samples/--missing-features")

        missing = [n for n in sequences if not self.windows[n].ppg]
        if missing:
            raise ValueError(f"{self.path}: {len(missing)} windows carry no signal data; "
                             f"re-export without --missing-samples")

        ppg = np.concatenate([np.frombuffer(self.windows[n].ppg, dtype='<f4')
                              for n in sequences])
        acc = np.concatenate([np.frombuffer(self.windows[n].acc, dtype='<f4')
                              for n in sequences])
        return ppg, acc
