cmake \
			-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
			-DCMAKE_BUILD_TYPE=Debug \
			-S . \
			-B .build


ln -sf \
			.build/compile_commands.json \
			./compile_commands.json


cmake --build .build
