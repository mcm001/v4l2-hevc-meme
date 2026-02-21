#!/bin/bash

# Run 10 instances of hevc_meme in parallel
for i in {1..9}; do
  ./build/hevc_meme "out${i}.h265" &
done

# Wait for all background processes to complete
wait
