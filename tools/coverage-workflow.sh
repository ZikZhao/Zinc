#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/coverage"
REPORT_DIR="$BUILD_DIR/reports"
LCOV_INFO="$REPORT_DIR/lcov.info"
HTML_DIR="$REPORT_DIR/html"

log() {
    printf '[coverage] %s\n' "$*"
}

is_wsl() {
    [[ -n "${WSL_INTEROP:-}" || -n "${WSL_DISTRO_NAME:-}" ]] && return 0
    grep -qi microsoft /proc/sys/kernel/osrelease 2>/dev/null
}

assert_wsl_runtime() {
    if ! is_wsl; then
        echo "ERROR: 'open' requires WSL runtime (uses explorer.exe + wslpath)." >&2
        exit 1
    fi
}

pick_gcov_tool() {
    if [[ -x /usr/bin/gcov-14 ]]; then
        echo "/usr/bin/gcov-14"
        return
    fi

    if command -v gcov >/dev/null 2>&1; then
        command -v gcov
        return
    fi

    echo "ERROR: gcov tool not found (checked /usr/bin/gcov-14 and gcov in PATH)." >&2
    exit 1
}

GCOV_TOOL="$(pick_gcov_tool)"

configure_coverage() {
    log "Configuring coverage build directory"
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" --preset debug \
        -DCMAKE_C_FLAGS=--coverage \
        -DCMAKE_CXX_FLAGS=--coverage \
        -DCMAKE_EXE_LINKER_FLAGS=--coverage \
        -DCMAKE_SHARED_LINKER_FLAGS=--coverage
}

build_coverage_targets() {
    configure_coverage
    log "Building coverage targets: unit_tests, zinc"
    cmake --build "$BUILD_DIR" --target unit_tests zinc
}

build_coverage_tests() {
    configure_coverage
    log "Building coverage target: unit_tests"
    cmake --build "$BUILD_DIR" --target unit_tests
}

build_coverage_main() {
    configure_coverage
    log "Building coverage target: zinc"
    cmake --build "$BUILD_DIR" --target zinc
}

reset_counters() {
    if [[ -d "$BUILD_DIR" ]]; then
        log "Resetting .gcda counters"
        find "$BUILD_DIR" -type f -name '*.gcda' -delete
    else
        log "build/coverage not found, skip reset"
    fi
}

run_coverage_tests() {
    build_coverage_tests
    run_coverage_tests_binary
}

run_coverage_tests_binary() {
    log "Running unit tests"
    "$BUILD_DIR/bin/unit_tests"
}

run_coverage_demos() {
    build_coverage_main
    run_coverage_demos_binary
}

run_coverage_demos_binary() {
    log "Compiling demos/**/main.zn with coverage zinc"

    local found=0
    while IFS= read -r -d '' demo; do
        found=1
        printf '[zinc] %s\n' "$demo"
        "$BUILD_DIR/bin/zinc" "$demo"
    done < <(find "$ROOT_DIR/demos" -type f -name main.zn -print0 | sort -z)

    if [[ "$found" -eq 0 ]]; then
        echo "ERROR: No demos/**/main.zn found." >&2
        exit 1
    fi
}

run_workloads() {
    build_coverage_targets
    reset_counters
    run_coverage_tests_binary
    run_coverage_demos_binary
    log "Coverage workloads finished"
}

generate_lcov() {
    mkdir -p "$REPORT_DIR"

    log "Capturing baseline coverage"
    lcov --gcov-tool "$GCOV_TOOL" \
        --ignore-errors mismatch,version \
        --rc geninfo_unexecuted_blocks=1 \
        --capture --initial \
        --directory "$BUILD_DIR" \
        --output-file "$REPORT_DIR/lcov.base.info"

    log "Capturing runtime coverage"
    lcov --gcov-tool "$GCOV_TOOL" \
        --ignore-errors mismatch,version \
        --rc geninfo_unexecuted_blocks=1 \
        --capture \
        --directory "$BUILD_DIR" \
        --output-file "$REPORT_DIR/lcov.run.info"

    log "Merging and filtering coverage"
    lcov -a "$REPORT_DIR/lcov.base.info" -a "$REPORT_DIR/lcov.run.info" \
        --output-file "$REPORT_DIR/lcov.raw.info"

    lcov --extract "$REPORT_DIR/lcov.raw.info" \
        "$ROOT_DIR/src/*" \
        "$ROOT_DIR/tests/*" \
        --output-file "$LCOV_INFO"

    rm -f "$REPORT_DIR/lcov.base.info" "$REPORT_DIR/lcov.run.info" "$REPORT_DIR/lcov.raw.info"
    log "Generated $LCOV_INFO"
}

generate_html() {
    log "Generating HTML report"
    genhtml "$LCOV_INFO" \
        --output-directory "$HTML_DIR" \
        --title "Zinc Coverage" \
        --legend \
        --show-details

    log "Generated $HTML_DIR/index.html"
}

open_report() {
    local report_file="$HTML_DIR/index.html"
    local report_windows_path

    if [[ ! -f "$report_file" ]]; then
        echo "ERROR: coverage report not found: $report_file" >&2
        exit 1
    fi

    assert_wsl_runtime

    if ! command -v wslpath >/dev/null 2>&1; then
        echo "ERROR: wslpath not found in PATH." >&2
        exit 1
    fi

    if ! command -v explorer.exe >/dev/null 2>&1; then
        echo "ERROR: explorer.exe not found in PATH." >&2
        exit 1
    fi

    report_windows_path="$(wslpath -w "$report_file")"
    if ! explorer.exe "$report_windows_path" >/dev/null 2>&1; then
        log "Could not auto-open browser. Open manually in Windows: $report_windows_path"
    fi

    printf '%s\n' "$report_file"
}

run_all() {
    run_workloads
    generate_lcov
    generate_html
    open_report
    log "Coverage pipeline complete"
}

usage() {
    cat <<'USAGE'
Usage: tools/coverage-workflow.sh <command>

Commands:
  configure     Configure coverage build directory
  build         Configure + build unit_tests and zinc
  build-tests   Configure + build unit_tests
  build-main    Configure + build zinc
  reset         Reset .gcda counters
  run-tests     Build + run coverage unit tests
  run-demos     Build + compile all demos/**/main.zn
  workloads     Build + reset + run tests + run demos
  lcov          Generate build/coverage/reports/lcov.info
  html          Generate build/coverage/reports/html/index.html
  open          Open HTML report in browser
  all           Workloads + lcov + html + open
USAGE
}

command_name="${1:-all}"

case "$command_name" in
    configure)
        configure_coverage
        ;;
    build)
        build_coverage_targets
        ;;
    build-tests)
        build_coverage_tests
        ;;
    build-main)
        build_coverage_main
        ;;
    reset)
        reset_counters
        ;;
    run-tests)
        run_coverage_tests
        ;;
    run-demos)
        run_coverage_demos
        ;;
    workloads)
        run_workloads
        ;;
    lcov)
        generate_lcov
        ;;
    html)
        generate_html
        ;;
    open)
        open_report
        ;;
    all)
        run_all
        ;;
    -h|--help|help)
        usage
        ;;
    *)
        echo "Unknown command: $command_name" >&2
        usage >&2
        exit 2
        ;;
esac
