import argparse
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", required=True)
    args = parser.parse_args()

    template = Path(args.source_dir) / "templates" / "condor" / "process.sh.in"
    text = template.read_text()
    assert 'sample_name = job.get("nickname")' in text
    assert 'print("SAMPLE_NAME=" + shlex.quote(sample_name))' in text
    assert '--set "channels.${CHANNEL}.sample_name=${SAMPLE_NAME}"' in text
    subprocess.run(["bash", "-n", str(template)], check=True)


if __name__ == "__main__":
    main()
