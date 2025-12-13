cppcheck --project=build/compile_commands.json --enable=all --suppress="*:third_party/*" --suppress=missingIncludeSystem  2> cppcheck_report.txt
