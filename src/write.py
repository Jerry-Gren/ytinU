import os

def save_code_to_file(target_directory, output_filename):
    """
    Recursively reads .py files and saves their path and content to a single output file.
    """
    
    # Check if target directory exists
    if not os.path.exists(target_directory):
        print(f"Error: The directory '{target_directory}' does not exist.")
        return

    try:
        # Open the output file in write mode ('w'). 
        # Use 'utf-8' encoding to handle Chinese characters correctly.
        with open(output_filename, 'w', encoding='utf-8') as out_f:
            
            # Recursively walk through the directory
            for root, dirs, files in os.walk(target_directory):
                if 'base' in dirs:
                    dirs.remove('base')
                for file in files:
                    if file.endswith('.cpp') or file.endswith('.h'):
                        # Construct full path
                        file_path = os.path.join(root, file)
                        # Normalize path separators
                        display_path = file_path.replace(os.sep, '/')
                        
                        try:
                            # Read the source code file
                            with open(file_path, 'r', encoding='utf-8') as in_f:
                                content = in_f.read()
                            
                            # Write formatted content to the output file
                            # Note: We need to manually add '\n' for line breaks
                            out_f.write(f"{display_path}:\n")
                            out_f.write("```\n")
                            out_f.write(content)
                            # Ensure content ends with a newline before closing the block
                            if not content.endswith('\n'):
                                out_f.write('\n') 
                            out_f.write("```\n\n") # Double newline for separation
                            
                            print(f"Processed: {display_path}") # Log progress to console
                            
                        except Exception as e:
                            print(f"Error reading {display_path}: {e}")
                            
        print(f"\nSuccess! All code has been saved to '{output_filename}'.")

    except Exception as e:
        print(f"Error creating output file: {e}")

if __name__ == "__main__":
    # Configuration
    TARGET_DIR = "."              # The directory you want to scan
    OUTPUT_FILE = "all_project_code.txt"  # The result file
    
    # Run
    save_code_to_file(TARGET_DIR, OUTPUT_FILE)
