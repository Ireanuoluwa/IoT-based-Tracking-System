@echo off
:: Stage all changes
git add .

:: Show status
git status

:: Prompt for commit message
set /p commit_message="Enter commit message: "

:: Commit with the message
git commit -m "%commit_message%"

:: Push to remote
git push
