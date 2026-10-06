#!/usr/bin/env python3
"""Offline beat analysis. Not imported by the game, simulator, or build gate.

Use a separate Python >=3.10 environment with beat-this==1.1.0, soundfile,
NumPy and PyTorch. Keep model weights and raw estimates under ignored build/.
Review meter/phase and uncertain passages before replacing the shipped grid.
"""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('audio', type=Path, help='Original, untrimmed source recording')
    parser.add_argument('--model', default='final0', help='Official checkpoint name or local file')
    parser.add_argument('--output', type=Path, required=True, help='Output JSON; sibling NPZ stores raw logits')
    args = parser.parse_args()
    import numpy as np
    import soundfile as sf
    import torch
    from beat_this.inference import Audio2Beats, Audio2Frames
    signal, rate = sf.read(args.audio, dtype='float32')
    torch.set_num_threads(4)
    model = Audio2Beats(checkpoint_path=args.model, device='cpu', dbn=False)
    logits = Audio2Frames.__call__(model, signal, rate)
    beats, downbeats = model.frames2beats(*logits)
    result = dict(source_sha256=hashlib.sha256(args.audio.read_bytes()).hexdigest(),
                  model=args.model, package=importlib.metadata.version('beat-this'),
                  torch=torch.__version__, sample_rate=rate,
                  beats=np.asarray(beats).tolist(), downbeats=np.asarray(downbeats).tolist(),
                  status='Unreviewed model estimates, not a game chart')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    np.savez(args.output.with_suffix('.npz'), beat=logits[0].numpy(), downbeat=logits[1].numpy())
    print(f'{len(beats)} beats, {len(downbeats)} downbeat candidates -> {args.output}')


if __name__ == '__main__':
    main()
