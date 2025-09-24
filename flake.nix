{
  description = "A Nix-based development environment for OpenRM-2024";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        # Set allowUnfree to true for proprietary software like CUDA and TensorRT
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };

        commonInputs = with pkgs; [
          gcc
          cmake
          pkg-config
          git

          # Core C++ libraries
          eigen
          ceres-solver
          ncurses.dev
        ];
      in
      {
        devShells = {
          # Default shell WITHOUT CUDA support
          # To use: nix develop
          default = pkgs.mkShell {
            name = "openrm-dev-nocuda";
            
            buildInputs = commonInputs ++ [
              # OpenCV without CUDA support, but with the required contrib modules
              (pkgs.opencv.override { enableContrib = true; enableCuda = false; })
            ];

            shellHook = ''
              # Set the compiler to the specified GCC version
              export CC=${pkgs.gcc}/bin/gcc
              export CXX=${pkgs.gcc}/bin/g++

              echo "✅ Entered OpenRM-2024 environment (No CUDA)."
              echo "   Compiler: $(g++ --version | head -n 1)"
              echo "   You can now build the project using: ./run.sh -t"
            '';
          };

          # Shell WITH CUDA support
          # To use: nix develop .#cuda
          cuda = pkgs.mkShell {
            name = "openrm-dev-cuda";
            
            buildInputs = commonInputs ++ (with pkgs; [
              # OpenCV built with support for both CUDA and contrib modules
              (opencv.override { enableContrib = true; enableCuda = true; })
              
              # NVIDIA-specific packages for GPU acceleration
              cudaPackages.cudatoolkit
              cudaPackages.cudnn
              cudaPackages.tensorrt

              cudaPackages.cuda_cccl   # Provides cudadevrt
              cudaPackages.cuda_cudart # Provides cudart_static
            ]);

            shellHook = ''
              # Set the compiler to the specified GCC version
              export CC=${pkgs.gcc}/bin/gcc
              export CXX=${pkgs.gcc}/bin/g++

              # CUDA fixes
              export CUDAARCHS="all"
              export LDFLAGS="-L${pkgs.cudaPackages.cuda_cccl.dev}/lib -L${pkgs.cudaPackages.cuda_cudart.static}/lib"

              echo "✅ Entered OpenRM-2024 environment (CUDA enabled)."
              echo "   Compiler: $(g++ --version | head -n 1)"
              echo "   CUDA:     $(nvcc --version | grep 'release')"
              echo ""
              echo "⚠️  IMPORTANT: Ensure your host system has a compatible NVIDIA driver installed!"
              echo ""
              echo "   You can now build the project using: ./run.sh -t"
            '';
          };
        };
      }
    );
}
