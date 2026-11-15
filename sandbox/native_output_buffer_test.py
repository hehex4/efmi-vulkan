"""Buffer SRVs coexist with native output changes, with image and hazard guards."""
from native_output_test import main

if __name__ == '__main__':
    raise SystemExit(main(buffers=True))
