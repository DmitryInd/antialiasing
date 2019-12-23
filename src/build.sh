if [ -d "../build" ]; then rm -Rf ../build; fi

mkdir ../build
gcc ./modules/main.c -o ../build/antialiassing
touch ../build/run.sh

echo "./antialiassing" >> ../build/run.sh

cp -r ./assets ../build/assets
