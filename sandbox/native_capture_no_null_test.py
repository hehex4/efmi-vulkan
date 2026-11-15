"""Exercise retained complete/cold transparency with nullDescriptor disabled."""
from native_translucency_depth_test import main

if __name__ == '__main__':
    raise SystemExit(main(no_null_descriptors=True))
