import subprocess
import sys
import re

def run_command(command):
    """Run the specified command and handle errors."""
    try:
        result = subprocess.run(command, shell=True, check=True, capture_output=True, text=True)
        print(result.stdout)
        return result.returncode
    except subprocess.CalledProcessError as e:
        print(f"Error occurred: {e.stderr}", file=sys.stderr)
        return e.returncode

def trigger_workflow(tag_name):
    """Trigger the GitHub Actions workflow using GitHub CLI."""
    # Set repository and branch
    repo = "GameAnalytics/GA-SDK-CPP-NEW"
    branch = "main"

    # Construct the GitHub CLI command
    command = f'gh workflow run "Create Release" --repo {repo} --ref {branch} -f tag_name={tag_name}'
    print(f"Running command: {command}")
    
    # Execute the command
    return_code = run_command(command)
    
    if return_code == 0:
        print("Workflow triggered successfully.")
    else:
        print(f"Failed to trigger workflow. Return code: {return_code}")

def check_gh_cli_installed():
    """Check if GitHub CLI (gh) is installed and available in the system path."""
    try:
        subprocess.run(['gh', '--version'], capture_output=True, check=True)
        return True
    except FileNotFoundError:
        return False

def validate_tag_name(tag_name):
    """Validate that the tag name matches the format 'v1.0.0'."""
    pattern = r"^v\d+\.\d+\.\d+$"
    if re.match(pattern, tag_name):
        return True
    else:
        return False

def main():
    # Ensure GitHub CLI is installed
    if not check_gh_cli_installed():
        print("GitHub CLI (gh) is not installed. Please install it and try again.")
        sys.exit(1)

    # Validate tag name passed as an argument
    if len(sys.argv) != 2:
        print("Usage: python trigger_workflow.py <tag_name>")
        sys.exit(1)

    tag_name = sys.argv[1]

    if not validate_tag_name(tag_name):
        print("Invalid tag name format. Please use the format 'v1.0.0'.")
        sys.exit(1)

    # Trigger the workflow with validated tag name
    trigger_workflow(tag_name)

if __name__ == "__main__":
    main()
