"""Null sampled images coexist with outputs only when the device feature allows them."""
from native_output_test import main

if __name__ == '__main__':
    raise SystemExit(main(null_images=True))
