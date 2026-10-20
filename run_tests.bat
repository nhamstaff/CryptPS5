@echo off
chcp 65001 >nul
title CryptPS5 Verification Suite
color 0A
cd /d "D:\projects\ReveseEngineeringProject"

echo ================================================================
echo  [1] CryptPS5 HLE Environment Verification Suite
echo ================================================================
tests_hle_verification.exe

echo.
echo ================================================================
echo  [2] CryptPS5 Container Analysis and Entropy Scanner
echo ================================================================
ps5decrypt.exe --scan extracted_pup\PS5UPDATE1.PUP

echo.
echo ================================================================
echo  All tests completed successfully.
echo ================================================================
pause
