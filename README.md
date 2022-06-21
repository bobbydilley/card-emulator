# Card Reader Emulator

A Sanwa Card reader/writer emulator written in C. The card reader emulator currently supports the following games:

- Derby Owners Club on Naomi
- Mario Kart Arcade GP 1 & 2 on Triforce

## Building & Running

Simply install the build-essential dependency and run make.

```
sudo apt install -y build-essential
make
```

To run the program simpy call it from the command line:

```
./build/card-emulator
```

## Issues

- Currently there is no control over inserting a new card
- Cards are only saved to ram and are deleted once the program is stopped
- You must change the settings via code
