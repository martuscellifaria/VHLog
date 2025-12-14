cppcheck --project=build/compile_commands.json --enable=all --check-level=exhaustive --suppress="*:third_party/*" --suppress=missingIncludeSystem  2> cppcheck_report.txt
