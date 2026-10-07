## Notes

Stepping Stones is an original memory game by Casey, made for kids: a safe path lights up
across a floor of tiles, then you walk it from memory. Source: https://github.com/sreenathkc/stepping-stones

## Controls

| Button | Action |
|--|--|
| D-Pad | Walk / move around the badge board |
| A | Play / next / choose |
| B | Back |
| L1 / R1 | Switch difficulty (easy / medium / hard) |
| Start | Menu (sounds, music, how to play) |
| Select + Start | Quit |

## Compile

```
git clone https://github.com/sreenathkc/stepping-stones.git
cd stepping-stones
make portmaster CROSS_CC=aarch64-linux-gnu-gcc
```
