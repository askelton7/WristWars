name: Build Pebble App

on:
  push:
  workflow_dispatch:

jobs:
  build:
    runs-on: ubuntu-24.04

    steps:
      - name: Get the code
        uses: actions/checkout@v4

      - name: Install uv
        run: |
          curl -LsSf https://astral.sh/uv/install.sh | sh
          echo "$HOME/.local/bin" >> $GITHUB_PATH

      - name: Install pebble-tool
        run: |
          uv tool install --force "pebble-tool==5.0.39" --python 3.13
          echo "$HOME/.local/bin" >> $GITHUB_PATH

      - name: Skip the analytics prompt
        run: |
          mkdir -p $HOME/.pebble-sdk
          touch $HOME/.pebble-sdk/NO_TRACKING

      - name: Install the Pebble SDK
        run: pebble sdk install latest

      - name: Let the SDK scaffold a fresh project
        run: pebble new-project /tmp/wristwars

      - name: Replace the scaffold source with ours
        run: |
          rm -f /tmp/wristwars/src/c/*.c
          cp src/c/main.c /tmp/wristwars/src/c/main.c
          ls -la /tmp/wristwars/src/c/

      - name: Point the project at Emery and name it
        run: |
          python3 - << 'PY'
          import json
          path = '/tmp/wristwars/package.json'
          data = json.load(open(path))
          data['pebble']['targetPlatforms'] = ['emery']
          data['pebble']['displayName'] = 'WristWars'
          json.dump(data, open(path, 'w'), indent=2)
          print(open(path).read())
          PY

      - name: Build
        working-directory: /tmp/wristwars
        run: pebble build

      - name: Upload the .pbw
        uses: actions/upload-artifact@v4
        with:
          name: wristwars-pbw
          path: /tmp/wristwars/build/*.pbw
          if-no-files-found: error
