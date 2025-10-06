{
  description = "A Nix-based development environment for OpenRM-2024";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    # nix2container.url = "github:nlewo/nix2container";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        # Set allowUnfree to true for proprietary software like CUDA and TensorRT
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };

        # ncurses.dev uses many, many symlinks
        # this triggers some edge-case when building the docker container
        # ncursesDevSanitized = pkgs.runCommand "ncurses-dev-sanitized" {} ''
        #   mkdir -p $out
        #   rsync -a \
        #     --copy-links \
        #     --exclude '/include/ncurses' \
        #     --exclude '/include/ncursesw' \
        #     ${pkgs.ncurses.dev}/ $out/
        # '';

        # main dev shell with cuda
        cudaDevShell = pkgs.mkShell {
          name = "openrm-dev-cuda";
          
          buildInputs = (with pkgs; [
            gcc
            cmake
            pkg-config
            git
            eigen
            ceres-solver
            ncursesDevSanitized
            # ncurses.dev
            
            # OpenCV with CUDA and contrib
            (opencv.override { enableContrib = true; enableCuda = true; })
            
            # NVIDIA packages
            cudaPackages.cudatoolkit
            cudaPackages.cudnn
            cudaPackages.tensorrt
            cudaPackages.cuda_cccl
            cudaPackages.cuda_cudart
          ]);

          # The environment setup logic, now separate for reusability
          shellHook = ''
            export CC=${pkgs.gcc}/bin/gcc
            export CXX=${pkgs.gcc}/bin/g++
            export CUDAARCHS="all"
            export LDFLAGS="-L${pkgs.cudaPackages.cuda_cccl.dev}/lib -L${pkgs.cudaPackages.cuda_cudart.static}/lib"
            
            # This makes the shell prompt nice
            export PS1="\[\033[01;32m\](openrm-cuda)\[\033[00m\]:\w\$ "
          '';
        };

        # Define the 'default' (no-cuda) dev shell
        defaultDevShell = pkgs.mkShell {
            name = "openrm-dev-nocuda";
            
            buildInputs = (with pkgs; [
                gcc
                cmake
                pkg-config
                git
                eigen
                ceres-solver
                ncurses.dev
                (opencv.override { enableContrib = true; enableCuda = false; })
            ]);

            shellHook = ''
              export CC=${pkgs.gcc}/bin/gcc
              export CXX=${pkgs.gcc}/bin/g++
              echo "✅ Entered OpenRM-2024 environment (No CUDA)."
            '';
        };

      in
      {
        # Your existing dev shells
        devShells = {
          default = defaultDevShell;
          cuda = cudaDevShell;
        };

        packages = let
          devEnvironment = pkgs.buildEnv {
            name = "openrm-env";
            paths = cudaDevShell.buildInputs;
          };
        in {
          # Image 1: A Docker image that drops you into the CUDA dev shell
          openrm-cuda = pkgs.dockerTools.buildImage {
            name = "openrm-dev";
            tag = "cuda";

            copyToRoot = [ devEnvironment pkgs.bashInteractive ];

            config = {
              Cmd = [
                "/bin/bash"
                "-c"
                "${cudaDevShell.shellHook} && exec bash"
              ];
            };
          };

          # Image 2: CUDA dev shell + a minimal VNC desktop environment
          openrm-cuda-desktop = pkgs.dockerTools.buildImage {
            name = "openrm-dev";
            tag = "desktop";

            # Add desktop packages on top of the clean environment
            contents = [ devEnvironment ] ++ (with pkgs; [
              bashInteractive
              xterm
              i3-gaps
              xorg.xinit
              tigervnc
            ]);

            config.ExposedPorts = { "5900/tcp" = {}; };
            config.Cmd = let
              entrypoint = pkgs.writeShellScriptBin "entrypoint.sh" ''
                #!${pkgs.bash}/bin/bash
                mkdir -p ~/.vnc
                echo "openrm" | vncpasswd -f > ~/.vnc/passwd
                chmod 600 ~/.vnc/passwd
                echo "🚀 Starting VNC server on port 5900 (password: openrm)"
                vncserver :1 -geometry 1280x800 -localhost no -fg &
                VNC_PID=$!
                export DISPLAY=:1
                sleep 2
                ${cudaDevShell.shellHook}
                i3 &
                xterm &
                wait $VNC_PID
              '';
            in [
              "${entrypoint}/bin/entrypoint.sh"
            ];
          };
        };

        # Add a default package for convenience (`nix build` without arguments)
        defaultPackage = self.packages.${system}.openrm-cuda;
      }
    );
}
