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

## Configuration

To specify which game to emulate, pass the `--game` parameter:

```
./build/card-emulator --game derby-owners-club
```

To specify the file path to the card data, pass the `--card` parameter:

```
./build/card-emulator --card mario-kart-bobby.bin
```

To specify the path to the serial convertor, pass the `--path` parameter:

```
./build/card-emulator --path /dev/ttyUSB1
```
